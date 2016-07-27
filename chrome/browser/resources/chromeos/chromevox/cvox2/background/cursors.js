// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview Classes related to cursors that point to and select parts of
 * the automation tree.
 */

goog.provide('cursors.Cursor');
goog.provide('cursors.Movement');
goog.provide('cursors.Range');
goog.provide('cursors.Unit');

goog.require('AutomationUtil');

/**
 * The special index that represents a cursor pointing to a node without
 * pointing to any part of its accessible text.
 */
cursors.NODE_INDEX = -1;

/**
 * Represents units of CursorMovement.
 * @enum {string}
 */
cursors.Unit = {
  /** A single character within accessible name or value. */
  CHARACTER: 'character',

  /** A range of characters (given by attributes on automation nodes). */
  WORD: 'word',

  /** A leaf node. */
  NODE: 'node',

  /** A leaf DOM-node. */
  DOM_NODE: 'dom_node',

  /** Formed by a set of leaf nodes that are inline. */
  LINE: 'line'
};

/**
 * Represents the ways in which cursors can move given a cursor unit.
 * @enum {string}
 */
cursors.Movement = {
  /** Move to the beginning or end of the current unit. */
  BOUND: 'bound',

  /** Move to the next unit in a particular direction. */
  DIRECTIONAL: 'directional'
};

goog.scope(function() {
var AutomationNode = chrome.automation.AutomationNode;
var Dir = AutomationUtil.Dir;
var Movement = cursors.Movement;
var Role = chrome.automation.RoleType;
var Unit = cursors.Unit;

/**
 * Represents a position within the automation tree.
 * @constructor
 * @param {!AutomationNode} node
 * @param {number} index A 0-based index into this cursor node's primary
 * accessible name. An index of |cursors.NODE_INDEX| means the node as a whole
 * is pointed to and covers the case where the accessible text is empty.
 */
cursors.Cursor = function(node, index) {
  /** @type {!AutomationNode} @private */
  this.node_ = node;
  /** @type {number} @private */
  this.index_ = index;
};

/**
 * Convenience method to construct a Cursor from a node.
 * @param {!AutomationNode} node
 * @return {!cursors.Cursor}
 */
cursors.Cursor.fromNode = function(node) {
  return new cursors.Cursor(node, cursors.NODE_INDEX);
};

cursors.Cursor.prototype = {
  /**
   * Returns true if |rhs| is equal to this cursor.
   * @param {!cursors.Cursor} rhs
   * @return {boolean}
   */
  equals: function(rhs) {
    return this.node_ === rhs.node &&
        this.index_ === rhs.index;
  },

  /**
   * @return {!AutomationNode}
   */
  get node() {
    return this.node_;
  },

  /**
   * @return {number}
   */
  get index() {
    return this.index_;
  },

  /**
   * Gets the accessible text of the node associated with this cursor.
   *
   * @param {!AutomationNode=} opt_node Use this node rather than this cursor's
   * node.
   * @return {string}
   */
  getText: function(opt_node) {
    var node = opt_node || this.node_;
    return node.name || '';
  },

  /**
   * Makes a Cursor which has been moved from this cursor by the unit in the
   * given direction using the given movement type.
   * @param {Unit} unit
   * @param {Movement} movement
   * @param {Dir} dir
   * @return {!cursors.Cursor} The moved cursor.
   */
  move: function(unit, movement, dir) {
    var newNode = this.node_;
    var newIndex = this.index_;

    if ((unit != Unit.NODE || unit != Unit.DOM_NODE) &&
        newIndex === cursors.NODE_INDEX)
      newIndex = 0;

    switch (unit) {
      case Unit.CHARACTER:
        // BOUND and DIRECTIONAL are the same for characters.
        newIndex = dir == Dir.FORWARD ? newIndex + 1 : newIndex - 1;
        if (newIndex < 0 || newIndex >= this.getText().length) {
          newNode = AutomationUtil.findNextNode(
              newNode, dir, AutomationPredicate.leafWithText);
          if (newNode) {
            newIndex =
                dir == Dir.FORWARD ? 0 : this.getText(newNode).length - 1;
            newIndex = newIndex == -1 ? 0 : newIndex;
          } else {
            newIndex = this.index_;
          }
        }
        break;
      case Unit.WORD:
        switch (movement) {
          case Movement.BOUND:
            if (newNode.role == Role.inlineTextBox) {
              var start, end;
              for (var i = 0; i < newNode.wordStarts.length; i++) {
                if (newIndex >= newNode.wordStarts[i] &&
                    newIndex <= newNode.wordEnds[i]) {
                  start = newNode.wordStarts[i];
                  end = newNode.wordEnds[i];
                  break;
                }
              }
              if (goog.isDef(start) && goog.isDef(end))
                newIndex = dir == Dir.FORWARD ? end : start;
            } else {
              // TODO(dtseng): Figure out what to do in this case.
            }
            break;
          case Movement.DIRECTIONAL:
            if (newNode.role == Role.inlineTextBox) {
              var start, end;
              for (var i = 0; i < newNode.wordStarts.length; i++) {
                if (newIndex >= newNode.wordStarts[i] &&
                    newIndex <= newNode.wordEnds[i]) {
                  var nextIndex = dir == Dir.FORWARD ? i + 1 : i - 1;
                  start = newNode.wordStarts[nextIndex];
                  end = newNode.wordEnds[nextIndex];
                  break;
                }
              }
              if (goog.isDef(start)) {
                newIndex = start;
              } else {
                // The backward case is special at the beginning of nodes.
                if (dir == Dir.BACKWARD && newIndex != 0) {
                  newIndex = 0;
                } else {
                  newNode = AutomationUtil.findNextNode(newNode, dir,
                      AutomationPredicate.leaf);
                  if (newNode) {
                    newIndex = 0;
                    if (dir == Dir.BACKWARD &&
                        newNode.role == Role.inlineTextBox) {
                      var starts = newNode.wordStarts;
                      newIndex = starts[starts.length - 1] || 0;
                    } else {
                      // TODO(dtseng): Figure out what to do for general nodes.
                    }
                  }
                }
              }
            } else {
              // TODO(dtseng): Figure out what to do in this case.
            }
        }
        break;
      case Unit.NODE:
      case Unit.DOM_NODE:
        switch (movement) {
          case Movement.BOUND:
            newIndex = dir == Dir.FORWARD ? this.getText().length - 1 : 0;
            break;
          case Movement.DIRECTIONAL:
            var pred = unit == Unit.NODE ?
                AutomationPredicate.leaf : AutomationPredicate.leafDomNode;
            newNode = AutomationUtil.findNextNode(
                newNode, dir, pred) || this.node_;
            newIndex = cursors.NODE_INDEX;
            break;
        }
        break;
      case Unit.LINE:
        newIndex = 0;
        switch (movement) {
          case Movement.BOUND:
            newNode = AutomationUtil.findNodeUntil(newNode, dir,
                AutomationPredicate.linebreak, {before: true});
            newNode = newNode || this.node_;
            newIndex =
                dir == Dir.FORWARD ? this.getText(newNode).length : 0;
            break;
          case Movement.DIRECTIONAL:
            newNode = AutomationUtil.findNodeUntil(
                newNode, dir, AutomationPredicate.linebreak);
            break;
          }
      break;
      default:
        throw 'Unrecognized unit: ' + unit;
    }
    newNode = newNode || this.node_;
    newIndex = goog.isDef(newIndex) ? newIndex : this.index_;
    return new cursors.Cursor(newNode, newIndex);
  }
};

/**
 * A cursors.Cursor that wraps from beginning to end and vice versa when moved.
 * @constructor
 * @param {!AutomationNode} node
 * @param {number} index A 0-based index into this cursor node's primary
 * accessible name. An index of |cursors.NODE_INDEX| means the node as a whole
 * is pointed to and covers the case where the accessible text is empty.
 * @extends {cursors.Cursor}
 */
cursors.WrappingCursor = function(node, index) {
  cursors.Cursor.call(this, node, index);
};


/**
 * Convenience method to construct a Cursor from a node.
 * @param {!AutomationNode} node
 * @return {!cursors.WrappingCursor}
 */
cursors.WrappingCursor.fromNode = function(node) {
  return new cursors.WrappingCursor(node, cursors.NODE_INDEX);
};

cursors.WrappingCursor.prototype = {
  __proto__: cursors.Cursor.prototype,

  /** @override */
  move: function(unit, movement, dir) {
    var result = cursors.Cursor.prototype.move.call(this, unit, movement, dir);
    if (movement == Movement.DIRECTIONAL && result.equals(this)) {
      var pred = unit == Unit.DOM_NODE ?
          AutomationPredicate.leafDomNode : AutomationPredicate.leaf;
      var root = this.node;
      while (!AutomationUtil.isTraversalRoot(root) && root.parent)
        root = root.parent;
      var wrappedNode = AutomationUtil.findNodePre(root, dir, pred);
      if (wrappedNode) {
        cvox.ChromeVox.earcons.playEarcon(cvox.Earcon.WRAP);
        return new cursors.WrappingCursor(wrappedNode, cursors.NODE_INDEX);
      }
    }
    return new cursors.WrappingCursor(result.node, result.index);
  }
};

/**
 * Represents a range in the automation tree. There is no visible selection on
 * the page caused by usage of this object.
 * It is assumed that the caller provides |start| and |end| in document order.
 * @param {!cursors.Cursor} start
 * @param {!cursors.Cursor} end
 * @constructor
 */
cursors.Range = function(start, end) {
  /** @type {!cursors.Cursor} @private */
  this.start_ = start;
  /** @type {!cursors.Cursor} @private */
  this.end_ = end;
};

/**
 * Convenience method to construct a Range surrounding one node.
 * @param {!AutomationNode} node
 * @return {!cursors.Range}
 */
cursors.Range.fromNode = function(node) {
  var cursor = cursors.WrappingCursor.fromNode(node);
  return new cursors.Range(cursor, cursor);
};

 /**
 * Given |rangeA| and |rangeB| in order, determine which |Dir|
 * relates them.
 * @param {!cursors.Range} rangeA
 * @param {!cursors.Range} rangeB
 * @return {Dir}
 */
cursors.Range.getDirection = function(rangeA, rangeB) {
  if (!rangeA || !rangeB)
    return Dir.FORWARD;

  // They are the same range.
  if (rangeA.start.node === rangeB.start.node &&
      rangeB.end.node === rangeA.end.node)
    return Dir.FORWARD;

  var testDirA =
      AutomationUtil.getDirection(
          rangeA.start.node, rangeB.end.node);
  var testDirB =
      AutomationUtil.getDirection(
          rangeB.start.node, rangeA.end.node);

  // The two ranges are either partly overlapping or non overlapping.
  if (testDirA == Dir.FORWARD && testDirB == Dir.BACKWARD)
    return Dir.FORWARD;
  else if (testDirA == Dir.BACKWARD && testDirB == Dir.FORWARD)
    return Dir.BACKWARD;
  else
    return testDirA;
};

cursors.Range.prototype = {
  /**
   * Returns true if |rhs| is equal to this range.
   * @param {!cursors.Range} rhs
   * @return {boolean}
   */
  equals: function(rhs) {
    return this.start_.equals(rhs.start) &&
        this.end_.equals(rhs.end);
  },

  /**
   * Gets a cursor bounding this range.
   * @param {Dir} dir Which endpoint cursor to return; Dir.FORWARD for end,
   * Dir.BACKWARD for start.
   * @param {boolean=} opt_reverse Specify to have Dir.BACKWARD return end,
   * Dir.FORWARD return start.
   * @return {!cursors.Cursor}
   */
  getBound: function(dir, opt_reverse) {
    if (opt_reverse)
      return dir == Dir.BACKWARD ? this.end_ : this.start_;
    return dir == Dir.FORWARD ? this.end_ : this.start_;
  },

  /**
   * @return {!cursors.Cursor}
   */
  get start() {
    return this.start_;
  },

  /**
   * @return {!cursors.Cursor}
   */
  get end() {
    return this.end_;
  },

  /**
   * Returns true if this range covers less than a node.
   * @return {boolean}
   */
  isSubNode: function() {
    return this.start.node === this.end.node &&
        this.start.index > -1 &&
        this.end.index > -1;
  },

  /**
   * Makes a Range which has been moved from this range by the given unit and
   * direction.
   * @param {Unit} unit
   * @param {Dir} dir
   * @return {cursors.Range}
   */
  move: function(unit, dir) {
    var newStart = this.start_;
    var newEnd = newStart;
    switch (unit) {
      case Unit.CHARACTER:
        newStart = newStart.move(unit, Movement.BOUND, dir);
        newEnd = newStart.move(unit, Movement.BOUND, Dir.FORWARD);
        // Character crossed a node; collapses to the end of the node.
        if (newStart.node !== newEnd.node)
          newEnd = newStart;
        break;
      case Unit.WORD:
      case Unit.LINE:
        newStart = newStart.move(unit, Movement.DIRECTIONAL, dir);
        newStart = newStart.move(unit, Movement.BOUND, Dir.BACKWARD);
        newEnd = newStart.move(unit, Movement.BOUND, Dir.FORWARD);
        break;
      case Unit.NODE:
      case Unit.DOM_NODE:
        newStart = newStart.move(unit, Movement.DIRECTIONAL, dir);
        newEnd = newStart;
        break;
    }
    return new cursors.Range(newStart, newEnd);
  }
};

});  // goog.scope
