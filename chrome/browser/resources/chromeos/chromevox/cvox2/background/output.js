// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview Provides output services for ChromeVox.
 */

goog.provide('Output');
goog.provide('Output.EventType');

goog.require('AutomationUtil.Dir');
goog.require('EarconEngine');
goog.require('Spannable');
goog.require('cursors.Cursor');
goog.require('cursors.Range');
goog.require('cursors.Unit');
goog.require('cvox.AbstractEarcons');
goog.require('cvox.NavBraille');
goog.require('cvox.ValueSelectionSpan');
goog.require('cvox.ValueSpan');
goog.require('goog.i18n.MessageFormat');

goog.scope(function() {
var Dir = AutomationUtil.Dir;

/**
 * An Output object formats a cursors.Range into speech, braille, or both
 * representations. This is typically a |Spannable|.
 *
 * The translation from Range to these output representations rely upon format
 * rules which specify how to convert AutomationNode objects into annotated
 * strings.
 * The format of these rules is as follows.
 *
 * $ prefix: used to substitute either an attribute or a specialized value from
 *     an AutomationNode. Specialized values include role and state.
 *     For example, $value $role $enabled
 * @ prefix: used to substitute a message. Note the ability to specify params to
 *     the message.  For example, '@tag_html' '@selected_index($text_sel_start,
 *     $text_sel_end').
 * @@ prefix: similar to @, used to substitute a message, but also pulls the
 *     localized string through goog.i18n.MessageFormat to support locale
 *     aware plural handling.  The first argument should be a number which will
 *     be passed as a COUNT named parameter to MessageFormat.
 *     TODO(plundblad): Make subsequent arguments normal placeholder arguments
 *     when needed.
 * = suffix: used to specify substitution only if not previously appended.
 *     For example, $name= would insert the name attribute only if no name
 * attribute had been inserted previously.
 * @constructor
 */
Output = function() {
  // TODO(dtseng): Include braille specific rules.
  /** @type {!Array<!Spannable>} */
  this.speechBuffer_ = [];
  /** @type {!Array<!Spannable>} */
  this.brailleBuffer_ = [];
  /** @type {!Array<!Object>} */
  this.locations_ = [];
  /** @type {function(?)} */
  this.speechEndCallback_;

  /**
   * Current global options.
   * @type {{speech: boolean, braille: boolean}}
   * @private
   */
  this.formatOptions_ = {speech: true, braille: false};

  /**
   * Speech properties to apply to the entire output.
   * @type {!Object<*>}
   * @private
   */
  this.speechProperties_ = {};

  /**
   * The speech category for the generated speech utterance.
   * @type {cvox.TtsCategory}
   * @private
   */
  this.speechCategory_ = cvox.TtsCategory.NAV;

  /**
   * The speech queue mode for the generated speech utterance.
   * @type {cvox.QueueMode}
   * @private
   */
  this.queueMode_ = cvox.QueueMode.QUEUE;
};

/**
 * Delimiter to use between output values.
 * @type {string}
 */
Output.SPACE = ' ';

/**
 * Metadata about supported automation roles.
 * @const {Object<{msgId: string,
 *                 earconId: (string|undefined),
 *                 inherits: (string|undefined)}>}
 * msgId: the message id of the role.
 * earconId: an optional earcon to play when encountering the role.
 * inherits: inherits rules from this role.
 * @private
 */
Output.ROLE_INFO_ = {
  alert: {
    msgId: 'role_alert',
    earconId: 'ALERT_NONMODAL',
  },
  alertDialog: {
    msgId: 'role_alertdialog'
  },
  article: {
    msgId: 'role_article',
    inherits: 'abstractContainer'
  },
  application: {
    msgId: 'role_application',
    inherits: 'abstractContainer'
  },
  banner: {
    msgId: 'role_banner',
    inherits: 'abstractContainer'
  },
  button: {
    msgId: 'role_button',
    earconId: 'BUTTON'
  },
  buttonDropDown: {
    msgId: 'role_button',
    earconId: 'BUTTON'
  },
  cell: {
    msgId: 'role_gridcell'
  },
  checkBox: {
    msgId: 'role_checkbox'
  },
  columnHeader: {
    msgId: 'role_columnheader',
    inherits: 'abstractContainer'
  },
  comboBox: {
    msgId: 'role_combobox'
  },
  complementary: {
    msgId: 'role_complementary',
    inherits: 'abstractContainer'
  },
  contentInfo: {
    msgId: 'role_contentinfo',
    inherits: 'abstractContainer'
  },
  date: {
    msgId: 'input_type_date',
    inherits: 'abstractContainer'
  },
  definition: {
    msgId: 'role_definition',
    inherits: 'abstractContainer'
  },
  dialog: {
    msgId: 'role_dialog'
  },
  directory: {
    msgId: 'role_directory',
    inherits: 'abstractContainer'
  },
  document: {
    msgId: 'role_document',
    inherits: 'abstractContainer'
  },
  form: {
    msgId: 'role_form',
    inherits: 'abstractContainer'
  },
  grid: {
    msgId: 'role_grid'
  },
  group: {
    msgId: 'role_group'
  },
  heading: {
    msgId: 'role_heading',
  },
  image: {
    msgId: 'role_img',
  },
  inputTime: {
    msgId: 'input_type_time',
    inherits: 'abstractContainer'
  },
  link: {
    msgId: 'role_link',
    earconId: 'LINK'
  },
  listBox: {
    msgId: 'role_listbox',
    earconId: 'LISTBOX'
  },
  listBoxOption: {
    msgId: 'role_listitem',
    earconId: 'LIST_ITEM'
  },
  listItem: {
    msgId: 'role_listitem',
    earconId: 'LIST_ITEM'
  },
  log: {
    msgId: 'role_log',
  },
  main: {
    msgId: 'role_main',
    inherits: 'abstractContainer'
  },
  marquee: {
    msgId: 'role_marquee',
  },
  math: {
    msgId: 'role_math',
    inherits: 'abstractContainer'
  },
  menu: {
    msgId: 'role_menu'
  },
  menuBar: {
    msgId: 'role_menubar',
  },
  menuItem: {
    msgId: 'role_menuitem',
    earconId: 'BUTTON'
  },
  menuItemCheckBox: {
    msgId: 'role_menuitemcheckbox',
    earconId: 'BUTTON'
  },
  menuItemRadio: {
    msgId: 'role_menuitemradio',
    earconId: 'BUTTON'
  },
  menuListOption: {
    msgId: 'role_menuitem'
  },
  menuListPopup: {
    msgId: 'role_menu'
  },
  navigation: {
    msgId: 'role_navigation',
    inherits: 'abstractContainer'
  },
  note: {
    msgId: 'role_note',
    inherits: 'abstractContainer'
  },
  popUpButton: {
    msgId: 'role_button',
  },
  radioButton: {
    msgId: 'role_radio'
  },
  radioGroup: {
    msgId: 'role_radiogroup',
  },
  region: {
    msgId: 'role_region',
    inherits: 'abstractContainer'
  },
  rowHeader: {
    msgId: 'role_rowheader',
    inherits: 'abstractContainer'
  },
  scrollBar: {
    msgId: 'role_scrollbar',
  },
  search: {
    msgId: 'role_search',
    inherits: 'abstractContainer'
  },
  separator: {
    msgId: 'role_separator',
    inherits: 'abstractContainer'
  },
  spinButton: {
    msgId: 'role_spinbutton',
    earconId: 'LISTBOX'
  },
  status: {
    msgId: 'role_status'
  },
  tab: {
    msgId: 'role_tab'
  },
  tabList: {
    msgId: 'role_tablist'
  },
  tabPanel: {
    msgId: 'role_tabpanel'
  },
  textBox: {
    msgId: 'input_type_text',
    earconId: 'EDITABLE_TEXT'
  },
  textField: {
    msgId: 'input_type_text',
    earconId: 'EDITABLE_TEXT'
  },
  time: {
    msgId: 'tag_time',
    inherits: 'abstractContainer'
  },
  timer: {
    msgId: 'role_timer'
  },
  toolbar: {
    msgId: 'role_toolbar'
  },
  tree: {
    msgId: 'role_tree'
  },
  treeItem: {
    msgId: 'role_treeitem'
  }
};

/**
 * Metadata about supported automation states.
 * @const {!Object<{on: {msgId: string, earconId: string},
 *                  off: {msgId: string, earconId: string},
 *                  omitted: {msgId: string, earconId: string}}>}
 *     on: info used to describe a state that is set to true.
 *     off: info used to describe a state that is set to false.
 *     omitted: info used to describe a state that is undefined.
 * @private
 */
Output.STATE_INFO_ = {
  checked: {
    on: {
      msgId: 'checkbox_checked_state'
    },
    off: {
      msgId: 'checkbox_unchecked_state'
    },
    omitted: {
      msgId: 'checkbox_unchecked_state'
    }
  },
  collapsed: {
    on: {
      msgId: 'aria_expanded_false'
    },
    off: {
      msgId: 'aria_expanded_true'
    }
  },
  expanded: {
    on: {
      msgId: 'aria_expanded_true'
    },
    off: {
      msgId: 'aria_expanded_false'
    }
  },
  visited: {
    on: {
      msgId: 'visited_state'
    }
  }
};

/**
 * Maps input types to message IDs.
 * @const {Object<string>}
 * @private
 */
Output.INPUT_TYPE_MESSAGE_IDS_ = {
  'email': 'input_type_email',
  'number': 'input_type_number',
  'password': 'input_type_password',
  'search': 'input_type_search',
  'tel': 'input_type_number',
  'text': 'input_type_text',
  'url': 'input_type_url',
};

/**
 * Rules specifying format of AutomationNodes for output.
 * @type {!Object<Object<Object<string>>>}
 */
Output.RULES = {
  navigate: {
    'default': {
      speak: '$name $value $role $description',
      braille: ''
    },
    abstractContainer: {
      enter: '$name $role $description',
      leave: '@exited_container($role)'
    },
    alert: {
      speak: '!doNotInterrupt $role $descendants'
    },
    alertDialog: {
      enter: '$name $role $description $descendants'
    },
    cell: {
      enter: '@column_granularity $tableCellColumnIndex'
    },
    checkBox: {
      speak: '$if($checked, $earcon(CHECK_ON), $earcon(CHECK_OFF)) ' +
             '$name $role $checked $description'
    },
    dialog: {
      enter: '$name $role $description'
    },
    div: {
      enter: '$name',
      speak: '$name $description'
    },
    grid: {
      enter: '$name $role $description'
    },
    heading: {
      enter: '@tag_h+$hierarchicalLevel',
      speak: '@tag_h+$hierarchicalLevel $nameOrDescendants='
    },
    inlineTextBox: {
      speak: '$name='
    },
    link: {
      enter: '$name $if($visited, @visited_link, $role)',
      stay: '$name= $if($visited, @visited_link, $role)',
      speak: '$name= $if($visited, @visited_link, $role) $description'
    },
    list: {
      enter: '$role @@list_with_items($countChildren(listItem))'
    },
    listBox: {
      enter: '$name $role @@list_with_items($countChildren(listBoxOption)) ' +
          '$description'
    },
    listBoxOption: {
      speak: '$name $role @describe_index($indexInParent, $parentChildCount) ' +
          '$description'
    },
    listItem: {
      enter: '$role'
    },
    menu: {
      enter: '$name $role @@list_with_items($countChildren(menuItem)) ' +
          '$description'
    },
    menuItem: {
      speak: '$name $role $if($haspopup, @has_submenu) ' +
          '@describe_index($indexInParent, $parentChildCount) ' +
          '$description'
    },
    menuListOption: {
      speak: '$name @role_menuitem ' +
          '@describe_index($indexInParent, $parentChildCount) $description'
    },
    paragraph: {
      speak: '$descendants'
    },
    popUpButton: {
      speak: '$earcon(POP_UP_BUTTON) $value $name $role @aria_has_popup ' +
          '$if($collapsed, @aria_expanded_false, @aria_expanded_true) ' +
          '$description'
    },
    radioButton: {
      speak: '$if($checked, @describe_radio_selected($name), ' +
          '@describe_radio_unselected($name)) $description'
    },
    radioGroup: {
      enter: '$name $role $description'
    },
    rootWebArea: {
      enter: '$name'
    },
    row: {
      enter: '@row_granularity $tableRowIndex'
    },
    slider: {
      speak: '$earcon(SLIDER) @describe_slider($value, $name) $description'
    },
    staticText: {
      speak: '$name='
    },
    tab: {
      speak: '@describe_tab($name)'
    },
    textField: {
      speak: '$name $value $if(' +
          '$inputType, $inputType, $role) $description',
      braille: ''
    },
    toolbar: {
      enter: '$name $role $description'
    },
    tree: {
      enter: '$name $role @@list_with_items($countChildren(treeItem))'
    },
    treeItem: {
      enter: '$role $expanded $collapsed ' +
          '@describe_index($indexInParent, $parentChildCount) ' +
          '@describe_depth($hierarchicalLevel)'
    },
    window: {
      enter: '$name',
      speak: '@describe_window($name) $earcon(OBJECT_OPEN)'
    }
  },
  menuStart: {
    'default': {
      speak: '@chrome_menu_opened($name)  $earcon(OBJECT_OPEN)'
    }
  },
  menuEnd: {
    'default': {
      speak: '@chrome_menu_closed $earcon(OBJECT_CLOSE)'
    }
  },
  menuListValueChanged: {
    'default': {
      speak: '$value $name ' +
          '$find({"state": {"selected": true, "invisible": false}}, ' +
              '@describe_index($indexInParent, $parentChildCount)) '
    }
  },
  alert: {
    default: {
      speak: '!doNotInterrupt ' +
          '@role_alert $name $earcon(ALERT_NONMODAL) $description $descendants'
    }
  }
};

/**
 * Custom actions performed while rendering an output string.
 * @constructor
 */
Output.Action = function() {
};

Output.Action.prototype = {
  run: function() {
  }
};

/**
 * Action to play an earcon.
 * @param {string} earconId
 * @constructor
 * @extends {Output.Action}
 */
Output.EarconAction = function(earconId) {
  Output.Action.call(this);
  /** @type {string} */
  this.earconId = earconId;
};

Output.EarconAction.prototype = {
  __proto__: Output.Action.prototype,

  /** @override */
  run: function() {
    cvox.ChromeVox.earcons.playEarcon(cvox.Earcon[this.earconId]);
  }
};

/**
 * Annotation for selection.
 * @param {number} startIndex
 * @param {number} endIndex
 * @constructor
 */
Output.SelectionSpan = function(startIndex, endIndex) {
  // TODO(dtseng): Direction lost below; should preserve for braille panning.
  this.startIndex = startIndex < endIndex ? startIndex : endIndex;
  this.endIndex = endIndex > startIndex ? endIndex : startIndex;
};

/**
 * Wrapper for automation nodes as annotations.  Since the
 * {@code chrome.automation.AutomationNode} constructor isn't exposed in
 * the API, this class is used to allow isntanceof checks on these
 * annotations.
 @ @param {chrome.automation.AutomationNode} node
 * @constructor
 */
Output.NodeSpan = function(node) {
  this.node = node;
};

/**
 * Possible events handled by ChromeVox internally.
 * @enum {string}
 */
Output.EventType = {
  NAVIGATE: 'navigate'
};

/**
 * If true, the next speech utterance will flush instead of the normal
 * queueing mode.
 * @type {boolean}
 * @private
 */
Output.flushNextSpeechUtterance_ = false;

/**
 * Calling this will make the next speech utterance flush even if it would
 * normally queue or do a category flush.
 */
Output.flushNextSpeechUtterance = function() {
  Output.flushNextSpeechUtterance_ = true;
};

Output.prototype = {
  /**
   * Gets the spoken output with separator '|'.
   * @return {!Spannable}
   */
  get speechOutputForTest() {
    return this.speechBuffer_.reduce(function(prev, cur) {
      if (prev === null)
        return cur;
      prev.append('|');
      prev.append(cur);
      return prev;
    }, null);
  },

  /**
   * Gets the output buffer for braille.
   * @return {!Spannable}
   */
  get brailleOutputForTest() {
    return this.createBrailleOutput_();
  },

  /**
   * @return {boolean} True if there's any speech that will be output.
   */
  get hasSpeech() {
    for (var i = 0; i < this.speechBuffer_.length; i++) {
      if (this.speechBuffer_[i].trim().length)
        return true;
    }
    return false;
  },

  /**
   * Specify ranges for speech.
   * @param {!cursors.Range} range
   * @param {cursors.Range} prevRange
   * @param {chrome.automation.EventType|Output.EventType} type
   * @return {!Output}
   */
  withSpeech: function(range, prevRange, type) {
    this.formatOptions_ = {speech: true, braille: false};
    this.render_(range, prevRange, type, this.speechBuffer_);
    return this;
  },

  /**
   * Specify ranges for braille.
   * @param {!cursors.Range} range
   * @param {cursors.Range} prevRange
   * @param {chrome.automation.EventType|Output.EventType} type
   * @return {!Output}
   */
  withBraille: function(range, prevRange, type) {
    this.formatOptions_ = {speech: false, braille: true};
    this.render_(range, prevRange, type, this.brailleBuffer_);
    return this;
  },

  /**
   * Specify ranges for location.
   * @param {!cursors.Range} range
   * @param {cursors.Range} prevRange
   * @param {chrome.automation.EventType|Output.EventType} type
   * @return {!Output}
   */
  withLocation: function(range, prevRange, type) {
    this.formatOptions_ = {speech: false, braille: false};
    this.render_(range, prevRange, type, [] /*unused output*/);
    return this;
  },

  /**
   * Specify the same ranges for speech and braille.
   * @param {!cursors.Range} range
   * @param {cursors.Range} prevRange
   * @param {chrome.automation.EventType|Output.EventType} type
   * @return {!Output}
   */
  withSpeechAndBraille: function(range, prevRange, type) {
    this.withSpeech(range, prevRange, type);
    this.withBraille(range, prevRange, type);
    return this;
  },

  /**
   * Applies the given speech category to the output.
   * @param {cvox.TtsCategory} category
   * @return {!Output}
   */
  withSpeechCategory: function(category) {
    this.speechCategory_ = category;
    return this;
  },

  /**
   * Applies the given speech queue mode to the output.
   * @param {cvox.QueueMode} queueMode The queueMode for the speech.
   * @return {!Output}
   */
  withQueueMode: function(queueMode) {
    this.queueMode_ = queueMode;
    return this;
  },

  /**
   * Apply a format string directly to the output buffer. This lets you
   * output a message directly to the buffer using the format syntax.
   * @param {string} formatStr
   * @param {!chrome.automation.AutomationNode=} opt_node An optional
   *     node to apply the formatting to.
   * @return {!Output}
   */
  format: function(formatStr, opt_node) {
    var node = opt_node || null;

    this.formatOptions_ = {speech: true, braille: false};
    this.format_(node, formatStr, this.speechBuffer_);

    this.formatOptions_ = {speech: false, braille: true};
    this.format_(node, formatStr, this.brailleBuffer_);

    return this;
  },

  /**
   * Triggers callback for a speech event.
   * @param {function()} callback
   */
  onSpeechEnd: function(callback) {
    this.speechEndCallback_ = function(opt_cleanupOnly) {
      if (!opt_cleanupOnly)
        callback();
    }.bind(this);
    return this;
  },

  /**
   * Executes all specified output.
   */
  go: function() {
    // Speech.
    var queueMode = this.queueMode_;
    if (Output.flushNextSpeechUtterance_) {
      queueMode = cvox.QueueMode.FLUSH;
      Output.flushNextSpeechUtterance_ = false;
    }

    this.speechProperties_.category = this.speechCategory_;

    this.speechBuffer_.forEach(function(buff, i, a) {
      (function() {
        var scopedBuff = buff;
        this.speechProperties_['startCallback'] = function() {
          var actions = scopedBuff.getSpansInstanceOf(Output.Action);
          if (actions) {
            actions.forEach(function(a) {
              a.run();
            });
          }
        };
      }.bind(this)());

      if (this.speechEndCallback_ && i == a.length - 1)
        this.speechProperties_['endCallback'] = this.speechEndCallback_;
      else
        this.speechProperties_['endCallback'] = null;
      cvox.ChromeVox.tts.speak(
          buff.toString(), queueMode, this.speechProperties_);
      queueMode = cvox.QueueMode.QUEUE;
    }.bind(this));

    // Braille.
    if (this.brailleBuffer_.length) {
      var buff = this.createBrailleOutput_();
      var selSpan =
          buff.getSpanInstanceOf(Output.SelectionSpan);
      var startIndex = -1, endIndex = -1;
      if (selSpan) {
        var valueStart = buff.getSpanStart(selSpan);
        var valueEnd = buff.getSpanEnd(selSpan);
        startIndex = valueStart + selSpan.startIndex;
        endIndex = valueStart + selSpan.endIndex;
        buff.setSpan(new cvox.ValueSpan(0), valueStart, valueEnd);
        buff.setSpan(new cvox.ValueSelectionSpan(), startIndex, endIndex);
      }

      var output = new cvox.NavBraille({
        text: buff,
        startIndex: startIndex,
        endIndex: endIndex
      });

      cvox.ChromeVox.braille.write(output);
    }

    // Display.
    if (cvox.ChromeVox.isChromeOS &&
        this.speechCategory_ != cvox.TtsCategory.LIVE) {
      chrome.accessibilityPrivate.setFocusRing(this.locations_);
    }
  },

  /**
   * Renders the given range using optional context previous range and event
   * type.
   * @param {!cursors.Range} range
   * @param {cursors.Range} prevRange
   * @param {chrome.automation.EventType|string} type
   * @param {!Array<Spannable>} buff Buffer to receive rendered output.
   * @private
   */
  render_: function(range, prevRange, type, buff) {
    if (range.isSubNode())
      this.subNode_(range, prevRange, type, buff);
    else
      this.range_(range, prevRange, type, buff);
  },

  /**
   * Format the node given the format specifier.
   * @param {chrome.automation.AutomationNode} node
   * @param {string|!Object} format The output format either specified as an
   * output template string or a parsed output format tree.
   * @param {!Array<Spannable>} buff Buffer to receive rendered output.
   * @param {!Object=} opt_exclude A set of attributes to exclude.
   * @private
   */
  format_: function(node, format, buff, opt_exclude) {
    opt_exclude = opt_exclude || {};
    var tokens = [];
    var args = null;

    // Hacky way to support args.
    if (typeof(format) == 'string') {
      format = format.replace(/([,:])\W/g, '$1');
      tokens = format.split(' ');
    } else {
      tokens = [format];
    }

    tokens.forEach(function(token) {
      // Ignore empty tokens.
      if (!token)
        return;

      // Parse the token.
      var tree;
      if (typeof(token) == 'string')
        tree = this.createParseTree_(token);
      else
        tree = token;

      // Obtain the operator token.
      token = tree.value;

      // Set suffix options.
      var options = {};
      options.annotation = [];
      options.isUnique = token[token.length - 1] == '=';
      if (options.isUnique)
        token = token.substring(0, token.length - 1);

      // Annotate braille output with the corresponding automation nodes
      // to support acting on nodes based on location in the output.
      if (this.formatOptions_.braille)
        options.annotation.push(new Output.NodeSpan(node));

      // Process token based on prefix.
      var prefix = token[0];
      token = token.slice(1);

      if (opt_exclude[token])
        return;

      // All possible tokens based on prefix.
      if (prefix == '$') {
        if (token == 'value') {
          var text = node.value;
          if (text !== undefined) {
            if (node.textSelStart !== undefined) {
              options.annotation.push(new Output.SelectionSpan(
                  node.textSelStart,
                  node.textSelEnd));
            }
          }
          // Annotate this as a name so we don't duplicate names from ancestors.
          if (node.role == chrome.automation.RoleType.inlineTextBox ||
              node.role == chrome.automation.RoleType.staticText)
            token = 'name';
          options.annotation.push(token);
          this.append_(buff, text, options);
        } else if (token == 'name') {
          options.annotation.push(token);
          if (this.formatOptions_.speech) {
            var earconFinder = node;
            while (earconFinder) {
              var info = Output.ROLE_INFO_[earconFinder.role];
              if (info && info.earconId) {
                options.annotation.push(
                    new Output.EarconAction(info.earconId));
                break;
              }
              earconFinder = earconFinder.parent;
            }
          }
          this.append_(buff, node.name, options);
        } else if (token == 'nameOrDescendants') {
          options.annotation.push(token);
          if (node.name)
            this.append_(buff, node.name, options);
          else
            this.format_(node, '$descendants', buff);
        } else if (token == 'indexInParent') {
          options.annotation.push(token);
          this.append_(buff, String(node.indexInParent + 1));
        } else if (token == 'parentChildCount') {
          options.annotation.push(token);
          if (node.parent)
            this.append_(buff, String(node.parent.children.length));
        } else if (token == 'state') {
          options.annotation.push(token);
          Object.getOwnPropertyNames(node.state).forEach(function(s) {
            this.append_(buff, s, options);
          }.bind(this));
        } else if (token == 'find') {
          // Find takes two arguments: JSON query string and format string.
          if (tree.firstChild) {
            var jsonQuery = tree.firstChild.value;
            node = node.find(
                /** @type {Object}*/(JSON.parse(jsonQuery)));
            var formatString = tree.firstChild.nextSibling;
            if (node)
              this.format_(node, formatString, buff);
          }
        } else if (token == 'descendants') {
          if (AutomationPredicate.leaf(node))
            return;

          // Construct a range to the leftmost and rightmost leaves.
          var leftmost = AutomationUtil.findNodePre(
              node, Dir.FORWARD, AutomationPredicate.leaf);
          var rightmost = AutomationUtil.findNodePre(
              node, Dir.BACKWARD, AutomationPredicate.leaf);
          if (!leftmost || !rightmost)
            return;

          var subrange = new cursors.Range(
              new cursors.Cursor(leftmost, 0),
              new cursors.Cursor(rightmost, 0));
          var prev = null;
          if (node)
            prev = cursors.Range.fromNode(node);
          this.range_(subrange, prev, 'navigate', buff);
        } else if (token == 'role') {
          options.annotation.push(token);
          var msg = node.role;
          var info = Output.ROLE_INFO_[node.role];
          if (info) {
            if (this.formatOptions_.braille)
              msg = Msgs.getMsg(info.msgId + '_brl');
            else
              msg = Msgs.getMsg(info.msgId);
          } else {
            console.error('Missing role info for ' + node.role);
          }
          this.append_(buff, msg, options);
        } else if (token == 'inputType') {
          if (!node.inputType)
            return;
          options.annotation.push(token);
          var msgId = Output.INPUT_TYPE_MESSAGE_IDS_[node.inputType] ||
              'input_type_text';
          if (this.formatOptions_.braille)
            msgId = msgId + '_brl';
          this.append_(buff, Msgs.getMsg(msgId), options);
        } else if (token == 'tableRowIndex' ||
            token == 'tableCellColumnIndex') {
          var value = node[token];
          if (!value)
            return;
          value = String(value + 1);
          options.annotation.push(token);
          this.append_(buff, value, options);
        } else if (node[token] !== undefined) {
          options.annotation.push(token);
          var value = node[token];
          if (typeof value == 'number')
            value = String(value);
          this.append_(buff, value, options);
        } else if (Output.STATE_INFO_[token]) {
          options.annotation.push('state');
          var stateInfo = Output.STATE_INFO_[token];
          var resolvedInfo = {};
          if (node.state[token] === undefined)
            resolvedInfo = stateInfo.omitted;
          else
            resolvedInfo = node.state[token] ? stateInfo.on : stateInfo.off;
          if (!resolvedInfo)
            return;
          if (this.formatOptions_.speech && resolvedInfo.earconId) {
            options.annotation.push(
                new Output.EarconAction(resolvedInfo.earconId));
          }
          var msgId =
              this.formatOptions_.braille ? resolvedInfo.msgId + '_brl' :
              resolvedInfo.msgId;
          var msg = Msgs.getMsg(msgId);
          this.append_(buff, msg, options);
        } else if (tree.firstChild) {
          // Custom functions.
          if (token == 'if') {
            var cond = tree.firstChild;
            var attrib = cond.value.slice(1);
            if (node[attrib] || node.state[attrib])
              this.format_(node, cond.nextSibling, buff);
            else
              this.format_(node, cond.nextSibling.nextSibling, buff);
          } else if (token == 'earcon') {
            // Ignore unless we're generating speech output.
            if (!this.formatOptions_.speech)
              return;

            options.annotation.push(
                new Output.EarconAction(tree.firstChild.value));
            this.append_(buff, '', options);
          } else if (token == 'countChildren') {
            var role = tree.firstChild.value;
            var count = node.children.filter(function(e) {
              return e.role == role;
            }).length;
            this.append_(buff, String(count));
          }
        }
      } else if (prefix == '@') {
        var isPluralized = (token[0] == '@');
        if (isPluralized)
          token = token.slice(1);
        // Tokens can have substitutions.
        var pieces = token.split('+');
        token = pieces.reduce(function(prev, cur) {
          var lookup = cur;
          if (cur[0] == '$')
            lookup = node[cur.slice(1)];
          return prev + lookup;
        }.bind(this), '');
        var msgId = token;
        var msgArgs = [];
        if (!isPluralized) {
          var curArg = tree.firstChild;
          while (curArg) {
            if (curArg.value[0] != '$') {
              console.error('Unexpected value: ' + curArg.value);
              return;
            }
            var msgBuff = [];
            this.format_(node, curArg, msgBuff);
            msgArgs = msgArgs.concat(msgBuff);
            curArg = curArg.nextSibling;
          }
        }
        var msg = Msgs.getMsg(msgId, msgArgs);
        try {
          if (this.formatOptions_.braille)
            msg = Msgs.getMsg(msgId + '_brl', msgArgs) || msg;
        } catch(e) {}

        if (!msg) {
          console.error('Could not get message ' + msgId);
          return;
        }

        if (isPluralized) {
          var arg = tree.firstChild;
          if (!arg || arg.nextSibling) {
            console.error('Pluralized messages take exactly one argument');
            return;
          }
          if (arg.value[0] != '$') {
            console.error('Unexpected value: ' + arg.value);
            return;
          }
          var argBuff = [];
          this.format_(node, arg, argBuff);
          var namedArgs = {COUNT: Number(argBuff[0])};
          msg = new goog.i18n.MessageFormat(msg).format(namedArgs);
        }

        this.append_(buff, msg, options);
      } else if (prefix == '!') {
        this.speechProperties_[token] = true;
      }
    }.bind(this));
  },

  /**
   * @param {!cursors.Range} range
   * @param {cursors.Range} prevRange
   * @param {chrome.automation.EventType|string} type
   * @param {!Array<Spannable>} rangeBuff
   * @private
   */
  range_: function(range, prevRange, type, rangeBuff) {
    if (!prevRange)
      prevRange = cursors.Range.fromNode(range.start.node.root);
    var cursor = cursors.Cursor.fromNode(range.start.node);
    var prevNode = prevRange.start.node;

    var formatNodeAndAncestors = function(node, prevNode) {
      var buff = [];
      this.ancestry_(node, prevNode, type, buff);
      this.node_(node, prevNode, type, buff);
      this.locations_.push(node.location);
      return buff;
    }.bind(this);

    while (cursor.node != range.end.node) {
      var node = cursor.node;
      rangeBuff.push.apply(rangeBuff, formatNodeAndAncestors(node, prevNode));
      prevNode = node;
      cursor = cursor.move(cursors.Unit.NODE,
                           cursors.Movement.DIRECTIONAL,
                           Dir.FORWARD);

      // Reached a boundary.
      if (cursor.node == prevNode)
        break;
    }
    var lastNode = range.end.node;
    rangeBuff.push.apply(rangeBuff, formatNodeAndAncestors(lastNode, prevNode));
  },

  /**
   * @param {!chrome.automation.AutomationNode} node
   * @param {!chrome.automation.AutomationNode} prevNode
   * @param {chrome.automation.EventType|string} type
   * @param {!Array<Spannable>} buff
   * @param {!Object=} opt_exclude A list of attributes to exclude from
   * processing.
   * @private
   */
  ancestry_: function(node, prevNode, type, buff, opt_exclude) {
    opt_exclude = opt_exclude || {};
    var prevUniqueAncestors =
        AutomationUtil.getUniqueAncestors(node, prevNode);
    var uniqueAncestors = AutomationUtil.getUniqueAncestors(prevNode, node);

    // First, look up the event type's format block.
    // Navigate is the default event.
    var eventBlock = Output.RULES[type] || Output.RULES['navigate'];

    var getMergedRoleBlock = function(role) {
      var parentRole = (Output.ROLE_INFO_[role] || {}).inherits;
      var roleBlock = eventBlock[role] || eventBlock['default'];
      var parentRoleBlock = parentRole ? eventBlock[parentRole] : {};
      var mergedRoleBlock = {};
      for (var key in parentRoleBlock)
        mergedRoleBlock[key] = parentRoleBlock[key];
      for (var key in roleBlock)
        mergedRoleBlock[key] = roleBlock[key];
      return mergedRoleBlock;
    };

    for (var i = 0, formatPrevNode;
         (formatPrevNode = prevUniqueAncestors[i]);
         i++) {
      var roleBlock = getMergedRoleBlock(formatPrevNode.role);
      if (roleBlock.leave)
        this.format_(formatPrevNode, roleBlock.leave, buff, opt_exclude);
    }

    var enterOutputs = [];
    var enterRole = {};
    for (var j = uniqueAncestors.length - 2, formatNode;
         (formatNode = uniqueAncestors[j]);
         j--) {
      var roleBlock = getMergedRoleBlock(formatNode.role);
      if (roleBlock.enter) {
        if (enterRole[formatNode.role])
          continue;
        enterRole[formatNode.role] = true;
        var tempBuff = [];
        this.format_(formatNode, roleBlock.enter, tempBuff, opt_exclude);
        enterOutputs.unshift(tempBuff);
      }
      if (formatNode.role == 'window')
        break;
    }
    enterOutputs.forEach(function(b) {
      buff.push.apply(buff, b);
    });

    if (!opt_exclude.stay) {
      var commonFormatNode = uniqueAncestors[0];
      while (commonFormatNode && commonFormatNode.parent) {
        commonFormatNode = commonFormatNode.parent;
        var roleBlock =
            eventBlock[commonFormatNode.role] || eventBlock['default'];
        if (roleBlock.stay)
          this.format_(commonFormatNode, roleBlock.stay, buff, opt_exclude);
      }
    }
  },

  /**
   * @param {!chrome.automation.AutomationNode} node
   * @param {!chrome.automation.AutomationNode} prevNode
   * @param {chrome.automation.EventType|string} type
   * @param {!Array<Spannable>} buff
   * @private
   */
  node_: function(node, prevNode, type, buff) {
    // Navigate is the default event.
    var eventBlock = Output.RULES[type] || Output.RULES['navigate'];
    var roleBlock = eventBlock[node.role] || eventBlock['default'];
    var speakFormat = roleBlock.speak || eventBlock['default'].speak;
    this.format_(node, speakFormat, buff);
  },

  /**
   * @param {!cursors.Range} range
   * @param {cursors.Range} prevRange
   * @param {chrome.automation.EventType|string} type
   * @param {!Array<Spannable>} buff
   * @private
   */
  subNode_: function(range, prevRange, type, buff) {
    if (!prevRange)
      prevRange = range;
    var dir = cursors.Range.getDirection(prevRange, range);
    var prevNode = prevRange.getBound(dir).node;
    this.ancestry_(
        range.start.node, prevNode, type, buff,
        {stay: true, name: true, value: true});
    var startIndex = range.start.index;
    var endIndex = range.end.index;
    if (startIndex === endIndex)
      endIndex++;
    this.append_(
        buff, range.start.getText().substring(startIndex, endIndex));
    this.locations_.push(
        range.start.node.boundsForRange(startIndex, endIndex));
  },

  /**
   * Appends output to the |buff|.
   * @param {!Array<Spannable>} buff
   * @param {string|!Spannable} value
   * @param {{isUnique: (boolean|undefined),
   *      annotation: !Array<*>}=} opt_options
   */
  append_: function(buff, value, opt_options) {
    opt_options = opt_options || {isUnique: false, annotation: []};

    // Reject empty values without annotations.
    if ((!value || value.length == 0) && opt_options.annotation.length == 0)
      return;

    var spannableToAdd = new Spannable(value);
    opt_options.annotation.forEach(function(a) {
      spannableToAdd.setSpan(a, 0, spannableToAdd.length);
    });

    // |isUnique| specifies an annotation that cannot be duplicated.
    if (opt_options.isUnique) {
      var annotationSansNodes = opt_options.annotation.filter(
          function(annotation) {
            return !(annotation instanceof Output.NodeSpan);
          });
      var alreadyAnnotated = buff.some(function(s) {
        return annotationSansNodes.some(function(annotation) {
          if (!s.hasSpan(annotation))
            return false;
          var start = s.getSpanStart(annotation);
          var end = s.getSpanEnd(annotation);
          return s.substring(start, end).toString() == value.toString();
        });
      });
      if (alreadyAnnotated)
        return;
    }

    buff.push(spannableToAdd);
  },

  /**
   * Parses the token containing a custom function and returns a tree.
   * @param {string} inputStr
   * @return {Object}
   * @private
   */
  createParseTree_: function(inputStr) {
    var root = {value: ''};
    var currentNode = root;
    var index = 0;
    var braceNesting = 0;
    while (index < inputStr.length) {
      if (inputStr[index] == '(') {
        currentNode.firstChild = {value: ''};
        currentNode.firstChild.parent = currentNode;
        currentNode = currentNode.firstChild;
      } else if (inputStr[index] == ')') {
        currentNode = currentNode.parent;
      } else if (inputStr[index] == '{') {
        braceNesting++;
        currentNode.value += inputStr[index];
      } else if (inputStr[index] == '}') {
        braceNesting--;
        currentNode.value += inputStr[index];
      } else if (inputStr[index] == ',' && braceNesting === 0) {
        currentNode.nextSibling = {value: ''};
        currentNode.nextSibling.parent = currentNode.parent;
        currentNode = currentNode.nextSibling;
      } else {
        currentNode.value += inputStr[index];
      }
      index++;
    }

    if (currentNode != root)
      throw 'Unbalanced parenthesis.';

    return root;
  },

  /**
   * Converts the currently rendered braille buffers to a single spannable.
   * @return {!Spannable}
   * @private
   */
  createBrailleOutput_: function() {
    var result = new Spannable();
    var separator = '';  // Changes to space as appropriate.
    this.brailleBuffer_.forEach(function(cur) {
      // If this chunk is empty, don't add it since it won't result
      // in any output on the braille display, but node spans would
      // start before the separator in that case, which is not desired.
      // The exception is if this chunk contains a selectionm, in which
      // case it will result in a cursor which has to be preserved.
      // In this case, having separators, potentially both before and after
      // the empty string is correct.
      if (cur.length == 0 && !cur.getSpanInstanceOf(Output.SelectionSpan))
        return;
      var spansToExtend = [];
      var spansToRemove = [];
      // Nodes that have node spans both on the character to the left
      // of the separator and to the right should also cover the separator.
      // We extend the left span to cover both the separator and what the
      // right span used to cover, removing the right span, mostly for
      // ease of writing tests and debug.
      // Note that getSpan(position) never returns zero length spans
      // (because they don't cover any position).  Still, we want to include
      // these because they can be included (the selection span in an empty
      // text field is an example), which is why we write the below code
      // using getSpansInstanceOf and check the endpoints (isntead of doing
      // the opposite).
      result.getSpansInstanceOf(Output.NodeSpan).forEach(function(leftSpan) {
        if (result.getSpanEnd(leftSpan) < result.length)
          return;
        var newEnd = result.length;
        cur.getSpansInstanceOf(Output.NodeSpan).forEach(function(rightSpan) {
          if (cur.getSpanStart(rightSpan) == 0 &&
              leftSpan.node === rightSpan.node) {
            newEnd = Math.max(
                newEnd,
                result.length + separator.length +
                    cur.getSpanEnd(rightSpan));
            spansToRemove.push(rightSpan);
          }
        });
        if (newEnd > result.length)
          spansToExtend.push({span: leftSpan, end: newEnd});
      });
      result.append(separator);
      result.append(cur);
      spansToExtend.forEach(function(elem) {
        result.setSpan(
            elem.span,
            result.getSpanStart(elem.span),
            elem.end);
      });
      spansToRemove.forEach(result.removeSpan.bind(result));
      separator = Output.SPACE;
    });
    return result;
  }
};

});  // goog.scope
