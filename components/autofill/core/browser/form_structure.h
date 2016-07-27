// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_AUTOFILL_CORE_BROWSER_FORM_STRUCTURE_H_
#define COMPONENTS_AUTOFILL_CORE_BROWSER_FORM_STRUCTURE_H_

#include <set>
#include <string>
#include <vector>

#include "base/callback.h"
#include "base/gtest_prod_util.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/scoped_vector.h"
#include "base/strings/string16.h"
#include "components/autofill/core/browser/autofill_field.h"
#include "components/autofill/core/browser/autofill_type.h"
#include "components/autofill/core/browser/field_types.h"
#include "url/gurl.h"

class XmlWriter;

enum UploadRequired {
  UPLOAD_NOT_REQUIRED,
  UPLOAD_REQUIRED,
  USE_UPLOAD_RATES
};

namespace base {
class TimeTicks;
}

namespace buzz {
class XmlElement;
}

namespace rappor {
class RapporService;
}

namespace autofill {

struct FormData;
struct FormDataPredictions;

// FormStructure stores a single HTML form together with the values entered
// in the fields along with additional information needed by Autofill.
class FormStructure {
 public:
  explicit FormStructure(const FormData& form);
  virtual ~FormStructure();

  // Runs several heuristics against the form fields to determine their possible
  // types.
  void DetermineHeuristicTypes();

  // Encodes the XML upload request from this FormStructure.
  // In some cases, a |login_form_signature| is included as part of the upload.
  // This field is empty when sending upload requests for non-login forms.
  bool EncodeUploadRequest(const ServerFieldTypeSet& available_field_types,
                           bool form_was_autofilled,
                           const std::string& login_form_signature,
                           std::string* encoded_xml) const;

  // Encodes a XML block contains autofill field type from this FormStructure.
  // This XML will be written VLOG only, never be sent to server. It will
  // help make FieldAssignments and feed back to autofill server as
  // experiment data.
  bool EncodeFieldAssignments(const ServerFieldTypeSet& available_field_types,
                              std::string* encoded_xml) const;

  // Encodes the XML query request for the set of |forms| that are valid (see
  // implementation for details on which forms are not included in the query).
  // The form signatures used in the Query request are output in
  // |encoded_signatures|. All valid fields are encoded in |encoded_xml|. For
  // example, there are three valid forms, with 2, 4, and 3 fields. The returned
  // XML would have type info for 9 fields, first two of which would be for the
  // first form, next 4 for the second, and the rest is for the third.
  static bool EncodeQueryRequest(const std::vector<FormStructure*>& forms,
                                 std::vector<std::string>* encoded_signatures,
                                 std::string* encoded_xml);

  // Parses the field types from the server query response. |forms| must be the
  // same as the one passed to EncodeQueryRequest when constructing the query.
  // |rappor_service| may be null.
  static void ParseQueryResponse(std::string response_xml,
                                 const std::vector<FormStructure*>& forms,
                                 rappor::RapporService* rappor_service);

  // Returns predictions using the details from the given |form_structures| and
  // their fields' predicted types.
  static std::vector<FormDataPredictions> GetFieldTypePredictions(
      const std::vector<FormStructure*>& form_structures);

  // The unique signature for this form, composed of the target url domain,
  // the form name, and the form field names in a 64-bit hash.
  std::string FormSignature() const;

  // Runs a quick heuristic to rule out forms that are obviously not
  // auto-fillable, like google/yahoo/msn search, etc.
  bool IsAutofillable() const;

  // Resets |autofill_count_| and counts the number of auto-fillable fields.
  // This is used when we receive server data for form fields.  At that time,
  // we may have more known fields than just the number of fields we matched
  // heuristically.
  void UpdateAutofillCount();

  // Returns true if this form matches the structural requirements for Autofill.
  bool ShouldBeParsed() const;

  // Returns true if we should query the crowdsourcing server to determine this
  // form's field types.  If the form includes author-specified types, this will
  // return false unless there are password fields in the form. If there are no
  // password fields the assumption is that the author has expressed their
  // intent and crowdsourced data should not be used to override this. Password
  // fields are different because there is no way to specify password generation
  // directly.
  bool ShouldBeCrowdsourced() const;

  // Sets the field types to be those set for |cached_form|.
  void UpdateFromCache(const FormStructure& cached_form);

  // Logs quality metrics for |this|, which should be a user-submitted form.
  // This method should only be called after the possible field types have been
  // set for each field.  |interaction_time| should be a timestamp corresponding
  // to the user's first interaction with the form.  |submission_time| should be
  // a timestamp corresponding to the form's submission.
  void LogQualityMetrics(const base::TimeTicks& load_time,
                         const base::TimeTicks& interaction_time,
                         const base::TimeTicks& submission_time,
                         rappor::RapporService* rappor_service,
                         bool did_show_suggestions) const;

  // Classifies each field in |fields_| based upon its |autocomplete| attribute,
  // if the attribute is available.  The association is stored into the field's
  // |heuristic_type|.
  // Fills |has_author_specified_types_| with |true| if the attribute is
  // available and neither empty nor set to the special values "on" or "off" for
  // at least one field.
  // Fills |has_author_specified_sections_| with |true| if the attribute
  // specifies a section for at least one field.
  void ParseFieldTypesFromAutocompleteAttributes();

  // Determines whether |type| and |field| match.
  typedef base::Callback<bool(ServerFieldType type,
                              const AutofillField& field)>
      InputFieldComparator;

  // Fills in |fields_| that match |types| (via |matches|) with info from
  // |get_info|. Uses |address_language_code| to determine line separators when
  // collapsing street address lines into a single-line input text field.
  bool FillFields(
      const std::vector<ServerFieldType>& types,
      const InputFieldComparator& matches,
      const base::Callback<base::string16(const AutofillType&)>& get_info,
      const std::string& address_language_code,
      const std::string& app_locale);

  // Returns the values that can be filled into the form structure for the
  // given type. For example, there's no way to fill in a value of "The Moon"
  // into ADDRESS_HOME_STATE if the form only has a
  // <select autocomplete="region"> with no "The Moon" option. Returns an
  // empty set if the form doesn't reference the given type or if all inputs
  // are accepted (e.g., <input type="text" autocomplete="region">).
  // All returned values are standardized to upper case.
  std::set<base::string16> PossibleValues(ServerFieldType type);

  // Gets the form's current value for |type|. For example, it may return
  // the contents of a text input or the currently selected <option>.
  base::string16 GetUniqueValue(HtmlFieldType type) const;

  const AutofillField* field(size_t index) const;
  AutofillField* field(size_t index);
  size_t field_count() const;

  // Returns the number of fields that are part of the form signature and that
  // are included in queries to the Autofill server.
  size_t active_field_count() const;

  // Returns the number of fields that are able to be autofilled.
  size_t autofill_count() const { return autofill_count_; }

  // Used for iterating over the fields.
  std::vector<AutofillField*>::const_iterator begin() const {
    return fields_.begin();
  }
  std::vector<AutofillField*>::const_iterator end() const {
    return fields_.end();
  }

  const base::string16& form_name() const { return form_name_; }

  const GURL& source_url() const { return source_url_; }

  bool has_author_specified_types() { return has_author_specified_types_; }

  bool has_author_specified_sections() {
    return has_author_specified_sections_;
  }

  void set_upload_required(UploadRequired required) {
    upload_required_ = required;
  }
  UploadRequired upload_required() const { return upload_required_; }

  // Returns a FormData containing the data this form structure knows about.
  FormData ToFormData() const;

  bool operator==(const FormData& form) const;
  bool operator!=(const FormData& form) const;

 private:
  friend class FormStructureTest;
  FRIEND_TEST_ALL_PREFIXES(AutofillDownloadTest, QueryAndUploadTest);

  // 64-bit hash of the string - used in FormSignature and unit-tests.
  static std::string Hash64Bit(const std::string& str);

  enum EncodeRequestType {
    QUERY,
    UPLOAD,
    FIELD_ASSIGNMENTS,
  };

  // Returns true if the form has no fields, or too many.
  bool IsMalformed() const;

  // Takes |xml_writer| and writes description for |fields_|, according to
  // |request_type|. Returns false on failure, including when there are no
  // fields, and true on success.
  bool EncodeFormRequest(EncodeRequestType request_type,
                         XmlWriter* xml_writer) const;

  // Classifies each field in |fields_| into a logical section.
  // Sections are identified by the heuristic that a logical section should not
  // include multiple fields of the same autofill type (with some exceptions, as
  // described in the implementation).  Sections are furthermore distinguished
  // as either credit card or non-credit card sections.
  // If |has_author_specified_sections| is true, only the second pass --
  // distinguishing credit card sections from non-credit card ones -- is made.
  void IdentifySections(bool has_author_specified_sections);

  // Returns true if field should be skipped when talking to Autofill server.
  bool ShouldSkipField(const FormFieldData& field) const;

  // The name of the form.
  base::string16 form_name_;

  // The source URL.
  GURL source_url_;

  // The target URL.
  GURL target_url_;

  // The number of fields able to be auto-filled.
  size_t autofill_count_;

  // A vector of all the input fields in the form.
  ScopedVector<AutofillField> fields_;

  // The number of fields that are part of the form signature and that are
  // included in queries to the Autofill server.
  size_t active_field_count_;

  // The names of the form input elements, that are part of the form signature.
  // The string starts with "&" and the names are also separated by the "&"
  // character. E.g.: "&form_input1_name&form_input2_name&...&form_inputN_name"
  std::string form_signature_field_names_;

  // Whether the server expects us to always upload, never upload, or default
  // to the stored upload rates.
  UploadRequired upload_required_;

  // Whether the form includes any field types explicitly specified by the site
  // author, via the |autocompletetype| attribute.
  bool has_author_specified_types_;

  // Whether the form includes any sections explicitly specified by the site
  // author, via the autocomplete attribute.
  bool has_author_specified_sections_;

  // Whether the form was parsed for autocomplete attribute, thus assigning
  // the real values of |has_author_specified_types_| and
  // |has_author_specified_sections_|.
  bool was_parsed_for_autocomplete_attributes_;

  // True if the form contains at least one password field.
  bool has_password_field_;

  // True if the form is a <form>.
  bool is_form_tag_;

  DISALLOW_COPY_AND_ASSIGN(FormStructure);
};

}  // namespace autofill

#endif  // COMPONENTS_AUTOFILL_CORE_BROWSER_FORM_STRUCTURE_H_
