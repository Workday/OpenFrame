// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/dbus/ibus/ibus_text.h"

#include "base/logging.h"
#include "base/values.h"
#include "chromeos/dbus/ibus/ibus_object.h"
#include "dbus/message.h"

namespace chromeos {

namespace {
const uint32 kAttributeUnderline = 1;  // Indicates underline attribute.
const uint32 kAttributeSelection = 2;  // Indicates background attribute.
const char kAnnotationKey[] = "annotation";
const char kDescriptionTitleKey[] = "description_title";
const char kDescriptionBodyKey[] = "description_body";

struct IBusAttribute {
  IBusAttribute() : type(0), value(0), start_index(0), end_index(0) {}
  uint32 type;
  uint32 value;
  uint32 start_index;
  uint32 end_index;
};

// Pops a IBusAttribute from |reader|.
// Returns false if an error occurs.
bool PopIBusAttribute(dbus::MessageReader* reader, IBusAttribute* attribute) {
  IBusObjectReader ibus_object_reader("IBusAttribute", reader);
  if (!ibus_object_reader.Init())
    return false;

  if (!ibus_object_reader.PopUint32(&attribute->type) ||
      !ibus_object_reader.PopUint32(&attribute->value) ||
      !ibus_object_reader.PopUint32(&attribute->start_index) ||
      !ibus_object_reader.PopUint32(&attribute->end_index)) {
    LOG(ERROR) << "Invalid variant structure[IBusAttribute]: "
               << "IBusAttribute should contain 4 unsigned integers.";
    return false;
  }
  return true;
}

// Appends a IBusAttribute into |writer|.
void AppendIBusAttribute(dbus::MessageWriter* writer,
                         const IBusAttribute& attribute) {
  IBusObjectWriter ibus_attribute_writer("IBusAttribute", "uuuu", writer);
  ibus_attribute_writer.CloseHeader();
  ibus_attribute_writer.AppendUint32(attribute.type);
  ibus_attribute_writer.AppendUint32(attribute.value);
  ibus_attribute_writer.AppendUint32(attribute.start_index);
  ibus_attribute_writer.AppendUint32(attribute.end_index);
  ibus_attribute_writer.CloseAll();
}

}  // namespace

void AppendIBusText(const IBusText& ibus_text, dbus::MessageWriter* writer) {
  IBusObjectWriter ibus_text_writer("IBusText", "sv", writer);

  if (!ibus_text.annotation().empty()) {
      scoped_ptr<base::Value> annotation(
          base::Value::CreateStringValue(ibus_text.annotation()));
      ibus_text_writer.AddAttachment(kAnnotationKey, *annotation.get());
  }
  if (!ibus_text.description_title().empty()) {
      scoped_ptr<base::Value> description_title(
          base::Value::CreateStringValue(ibus_text.description_title()));
      ibus_text_writer.AddAttachment(kDescriptionTitleKey,
                                     *description_title.get());
  }
  if (!ibus_text.description_body().empty()) {
      scoped_ptr<base::Value> description_body(
          base::Value::CreateStringValue(ibus_text.description_body()));
      ibus_text_writer.AddAttachment(kDescriptionBodyKey,
                                     *description_body.get());
  }
  ibus_text_writer.CloseHeader();

  ibus_text_writer.AppendString(ibus_text.text());

  // Start appending IBusAttrList into IBusText
  IBusObjectWriter ibus_attr_list_writer("IBusAttrList", "av", NULL);
  ibus_text_writer.AppendIBusObject(&ibus_attr_list_writer);
  ibus_attr_list_writer.CloseHeader();
  dbus::MessageWriter attribute_array_writer(NULL);
  ibus_attr_list_writer.OpenArray("v", &attribute_array_writer);

  const std::vector<IBusText::UnderlineAttribute>& underline_attributes =
      ibus_text.underline_attributes();
  for (size_t i = 0; i < underline_attributes.size(); ++i) {
    IBusAttribute attribute;
    attribute.type = kAttributeUnderline;
    attribute.value = static_cast<uint32>(underline_attributes[i].type);
    attribute.start_index = underline_attributes[i].start_index;
    attribute.end_index = underline_attributes[i].end_index;
    AppendIBusAttribute(&attribute_array_writer, attribute);
  }

  const std::vector<IBusText::SelectionAttribute>& selection_attributes =
      ibus_text.selection_attributes();
  for (size_t i = 0; i < selection_attributes.size(); ++i) {
    IBusAttribute attribute;
    attribute.type = kAttributeSelection;
    attribute.value = 0;
    attribute.start_index = selection_attributes[i].start_index;
    attribute.end_index = selection_attributes[i].end_index;
    AppendIBusAttribute(&attribute_array_writer, attribute);
  }

  // Close all writers.
  ibus_attr_list_writer.CloseContainer(&attribute_array_writer);
  ibus_attr_list_writer.CloseAll();
  ibus_text_writer.CloseAll();
}

void CHROMEOS_EXPORT AppendStringAsIBusText(const std::string& text,
                                            dbus::MessageWriter* writer) {
  IBusText ibus_text;
  ibus_text.set_text(text);
  AppendIBusText(ibus_text, writer);
}

bool PopIBusText(dbus::MessageReader* reader, IBusText* ibus_text) {
  IBusObjectReader ibus_text_reader("IBusText", reader);

  if (!ibus_text_reader.Init())
    return false;

  const base::Value* annotation_value =
      ibus_text_reader.GetAttachment(kAnnotationKey);
  if (annotation_value) {
    std::string annotation;
    if (annotation_value->GetAsString(&annotation))
      ibus_text->set_annotation(annotation);
  }

  const base::Value* description_title_value =
      ibus_text_reader.GetAttachment(kDescriptionTitleKey);
  if (description_title_value) {
    std::string description_title;
    if (description_title_value->GetAsString(&description_title))
      ibus_text->set_description_title(description_title);
  }

  const base::Value* description_body_value =
      ibus_text_reader.GetAttachment(kDescriptionBodyKey);
  if (description_body_value) {
    std::string description_body;
    if (description_body_value->GetAsString(&description_body))
      ibus_text->set_description_body(description_body);
  }

  std::string text;
  if (!ibus_text_reader.PopString(&text)) {
    LOG(ERROR) << "Invalid variant structure[IBusText]: "
               << "1st argument should be string.";
    return false;
  }

  ibus_text->set_text(text);

  // Start reading IBusAttrList object
  IBusObjectReader ibus_attr_list_reader("IBusAttrList", NULL);
  if (!ibus_text_reader.PopIBusObject(&ibus_attr_list_reader)) {
    LOG(ERROR) << "Invalid variant structure[IBusText]: "
               << "2nd argument should be IBusAttrList.";
    return false;
  }

  dbus::MessageReader attribute_array_reader(NULL);
  if (!ibus_attr_list_reader.PopArray(&attribute_array_reader)) {
    LOG(ERROR) << "Invalid variant structure[IBusAttrList]: "
               << "1st argument should be array of IBusAttribute.";
    return false;
  }

  std::vector<IBusText::UnderlineAttribute>* underline_attributes =
      ibus_text->mutable_underline_attributes();

  std::vector<IBusText::SelectionAttribute>* selection_attributes =
      ibus_text->mutable_selection_attributes();

  while (attribute_array_reader.HasMoreData()) {
    IBusAttribute attribute;
    if (!PopIBusAttribute(&attribute_array_reader, &attribute))
      return false;

    if (attribute.type == kAttributeUnderline) {
      IBusText::UnderlineAttribute underline_attribute;
      underline_attribute.type =
          static_cast<IBusText::IBusTextUnderlineType>(attribute.value);
      underline_attribute.start_index = attribute.start_index;
      underline_attribute.end_index = attribute.end_index;
      underline_attributes->push_back(underline_attribute);
    } else if (attribute.type == kAttributeSelection) {
      IBusText::SelectionAttribute selection_attribute;
      selection_attribute.start_index = attribute.start_index;
      selection_attribute.end_index = attribute.end_index;
      selection_attributes->push_back(selection_attribute);
    } else {
      DVLOG(1) << "Chrome does not support background attribute.";
    }
  }

  return true;
}

bool CHROMEOS_EXPORT PopStringFromIBusText(dbus::MessageReader* reader,
                                           std::string* text) {
  IBusText ibus_text;
  if (!PopIBusText(reader, &ibus_text))
    return false;
  *text = ibus_text.text();
  return true;
}

///////////////////////////////////////////////////////////////////////////////
// IBusText
IBusText::IBusText()
    : text_("") {
}

IBusText::~IBusText() {
}

void IBusText::CopyFrom(const IBusText& obj) {
  text_ = obj.text();
  annotation_ = obj.annotation();
  description_title_ = obj.description_title();
  description_body_ = obj.description_body();
  underline_attributes_ = obj.underline_attributes();
  selection_attributes_ = obj.selection_attributes();
}

}  // namespace chromeos
