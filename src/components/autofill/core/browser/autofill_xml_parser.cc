// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/autofill/core/browser/autofill_xml_parser.h"

#include <stdlib.h>
#include <string.h>

#include "base/logging.h"
#include "base/strings/string_number_conversions.h"
#include "components/autofill/content/browser/autocheckout_page_meta_data.h"
#include "components/autofill/core/browser/autofill_server_field_info.h"
#include "third_party/libjingle/source/talk/xmllite/qname.h"

namespace autofill {

AutofillXmlParser::AutofillXmlParser()
    : succeeded_(true) {
}

AutofillXmlParser::~AutofillXmlParser() {}

void AutofillXmlParser::CharacterData(
    buzz::XmlParseContext* context, const char* text, int len) {
}

void AutofillXmlParser::EndElement(buzz::XmlParseContext* context,
                                   const char* name) {
}

void AutofillXmlParser::Error(buzz::XmlParseContext* context,
                              XML_Error error_code) {
  succeeded_ = false;
}

AutofillQueryXmlParser::AutofillQueryXmlParser(
    std::vector<AutofillServerFieldInfo>* field_infos,
    UploadRequired* upload_required,
    std::string* experiment_id,
    AutocheckoutPageMetaData* page_meta_data)
    : field_infos_(field_infos),
      upload_required_(upload_required),
      experiment_id_(experiment_id),
      page_meta_data_(page_meta_data),
      current_click_element_(NULL),
      current_page_number_for_page_types_(0),
      is_in_type_section_(false) {
  DCHECK(upload_required_);
  DCHECK(experiment_id_);
  DCHECK(page_meta_data_);
}

AutofillQueryXmlParser::~AutofillQueryXmlParser() {}

void AutofillQueryXmlParser::StartElement(buzz::XmlParseContext* context,
                                          const char* name,
                                          const char** attrs) {
  buzz::QName qname = context->ResolveQName(name, false);
  const std::string& element = qname.LocalPart();
  if (element.compare("autofillqueryresponse") == 0) {
    // We check for the upload required attribute below, but if it's not
    // present, we use the default upload rates. Likewise, by default we assume
    // an empty experiment id.
    *upload_required_ = USE_UPLOAD_RATES;
    *experiment_id_ = std::string();

    // |attrs| is a NULL-terminated list of (attribute, value) pairs.
    while (*attrs) {
      buzz::QName attribute_qname = context->ResolveQName(*attrs, true);
      ++attrs;
      const std::string& attribute_name = attribute_qname.LocalPart();
      if (attribute_name.compare("uploadrequired") == 0) {
        if (strcmp(*attrs, "true") == 0)
          *upload_required_ = UPLOAD_REQUIRED;
        else if (strcmp(*attrs, "false") == 0)
          *upload_required_ = UPLOAD_NOT_REQUIRED;
      } else if (attribute_name.compare("experimentid") == 0) {
        *experiment_id_ = *attrs;
      }
      ++attrs;
    }
  } else if (element.compare("field") == 0) {
    if (!*attrs) {
      // Missing the "autofilltype" attribute, abort.
      context->RaiseError(XML_ERROR_ABORTED);
      return;
    }

    // Determine the field type from the attribute value.  There should be one
    // attribute (autofilltype) with an integer value.
    AutofillServerFieldInfo field_info;
    field_info.field_type = UNKNOWN_TYPE;

    // |attrs| is a NULL-terminated list of (attribute, value) pairs.
    while (*attrs) {
      buzz::QName attribute_qname = context->ResolveQName(*attrs, true);
      ++attrs;
      const std::string& attribute_name = attribute_qname.LocalPart();
      if (attribute_name.compare("autofilltype") == 0) {
        int value = GetIntValue(context, *attrs);
        if (value >= 0 && value < MAX_VALID_FIELD_TYPE)
          field_info.field_type = static_cast<ServerFieldType>(value);
        else
          field_info.field_type = NO_SERVER_DATA;
      } else if (field_info.field_type == FIELD_WITH_DEFAULT_VALUE &&
                 attribute_name.compare("defaultvalue") == 0) {
        field_info.default_value = *attrs;
      }
      ++attrs;
    }

    // Record this field type, default value pair.
    field_infos_->push_back(field_info);
  } else if (element.compare("autofill_flow") == 0) {
    // |attrs| is a NULL-terminated list of (attribute, value) pairs.
    while (*attrs) {
      buzz::QName attribute_qname = context->ResolveQName(*attrs, true);
      ++attrs;
      const std::string& attribute_name = attribute_qname.LocalPart();
      if (attribute_name.compare("page_no") == 0)
        page_meta_data_->current_page_number = GetIntValue(context, *attrs);
      else if (attribute_name.compare("total_pages") == 0)
        page_meta_data_->total_pages = GetIntValue(context, *attrs);
      else if (attribute_name.compare("ignore_ajax") == 0)
        page_meta_data_->ignore_ajax = strcmp(*attrs, "false") != 0;
      ++attrs;
    }
  } else if (element.compare("page_advance_button") == 0) {
    page_meta_data_->proceed_element_descriptor = WebElementDescriptor();
    ParseElementDescriptor(context,
                           attrs,
                           &page_meta_data_->proceed_element_descriptor);
  } else if (element.compare("click_elements_before_formfill") == 0) {
    page_meta_data_->click_elements_before_form_fill.push_back(
        WebElementDescriptor());
    current_click_element_ = &page_meta_data_->click_elements_before_form_fill.
        back();
  } else if (element.compare("click_elements_after_formfill") == 0) {
    page_meta_data_->click_elements_after_form_fill.push_back(
        WebElementDescriptor());
    current_click_element_ = &page_meta_data_->click_elements_after_form_fill.
        back();
  } else if (element.compare("web_element") == 0) {
    ParseElementDescriptor(context, attrs, current_click_element_);
  } else if (element.compare("flow_page") == 0) {
    while (*attrs) {
      buzz::QName attribute_qname = context->ResolveQName(*attrs, true);
      ++attrs;
      const std::string& attribute_name = attribute_qname.LocalPart();
      if (attribute_name.compare("page_no") == 0)
        current_page_number_for_page_types_ = GetIntValue(context, *attrs);
      ++attrs;
    }
  } else if (element.compare("type") == 0) {
    is_in_type_section_ = true;
  }
}

void AutofillQueryXmlParser::ParseElementDescriptor(
    buzz::XmlParseContext* context,
    const char* const* attrs,
    WebElementDescriptor* element_descriptor) {
  // If both id and css_selector are set, the first one to appear will take
  // precedence.
  // |attrs| is a NULL-terminated list of (attribute, value) pairs.
  while (*attrs) {
    buzz::QName attribute_qname = context->ResolveQName(*attrs, true);
    ++attrs;
    const std::string& attribute_name = attribute_qname.LocalPart();
    buzz::QName value_qname = context->ResolveQName(*attrs, true);
    ++attrs;
    const std::string& attribute_value = value_qname.LocalPart();
    if (attribute_name.compare("id") == 0 && !attribute_value.empty()) {
      element_descriptor->retrieval_method = autofill::WebElementDescriptor::ID;
      element_descriptor->descriptor = attribute_value;
      break;
    } else if (attribute_name.compare("css_selector") == 0 &&
               !attribute_value.empty()) {
      element_descriptor->retrieval_method =
          autofill::WebElementDescriptor::CSS_SELECTOR;
      element_descriptor->descriptor = attribute_value;
      break;
    }
  }
}

void AutofillQueryXmlParser::EndElement(buzz::XmlParseContext* context,
                                   const char* name) {
  is_in_type_section_ = false;
}

void AutofillQueryXmlParser::CharacterData(
    buzz::XmlParseContext* context, const char* text, int len) {
  if (!is_in_type_section_)
    return;

  int type = -1;
  base::StringToInt(std::string(text, len), &type);
  if (type >= AUTOCHECKOUT_STEP_MIN_VALUE &&
      type <= AUTOCHECKOUT_STEP_MAX_VALUE) {
    AutocheckoutStepType step_type =
        static_cast<AutocheckoutStepType>(type);
    page_meta_data_->page_types[current_page_number_for_page_types_]
        .push_back(step_type);
  }
}

int AutofillQueryXmlParser::GetIntValue(buzz::XmlParseContext* context,
                                        const char* attribute) {
  char* attr_end = NULL;
  int value = strtol(attribute, &attr_end, 10);
  if (attr_end != NULL && attr_end == attribute) {
    context->RaiseError(XML_ERROR_SYNTAX);
    return 0;
  }
  return value;
}

AutofillUploadXmlParser::AutofillUploadXmlParser(double* positive_upload_rate,
                                                 double* negative_upload_rate)
    : succeeded_(false),
      positive_upload_rate_(positive_upload_rate),
      negative_upload_rate_(negative_upload_rate) {
  DCHECK(positive_upload_rate_);
  DCHECK(negative_upload_rate_);
}

void AutofillUploadXmlParser::StartElement(buzz::XmlParseContext* context,
                                           const char* name,
                                           const char** attrs) {
  buzz::QName qname = context->ResolveQName(name, false);
  const std::string &element = qname.LocalPart();
  if (element.compare("autofilluploadresponse") == 0) {
    // Loop over all attributes to get the upload rates.
    while (*attrs) {
      buzz::QName attribute_qname = context->ResolveQName(attrs[0], true);
      const std::string &attribute_name = attribute_qname.LocalPart();
      if (attribute_name.compare("positiveuploadrate") == 0) {
        *positive_upload_rate_ = GetDoubleValue(context, attrs[1]);
      } else if (attribute_name.compare("negativeuploadrate") == 0) {
        *negative_upload_rate_ = GetDoubleValue(context, attrs[1]);
      }
      attrs += 2;  // We peeked at attrs[0] and attrs[1], skip past both.
    }
  }
}

double AutofillUploadXmlParser::GetDoubleValue(buzz::XmlParseContext* context,
                                               const char* attribute) {
  char* attr_end = NULL;
  double value = strtod(attribute, &attr_end);
  if (attr_end != NULL && attr_end == attribute) {
    context->RaiseError(XML_ERROR_SYNTAX);
    return 0.0;
  }
  return value;
}

}  // namespace autofill
