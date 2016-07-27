/*
 * Copyright (C) 2010 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * @constructor
 * @extends {WebInspector.SDKModel}
 * @param {!WebInspector.Target} target
 */
WebInspector.CSSStyleModel = function(target)
{
    WebInspector.SDKModel.call(this, WebInspector.CSSStyleModel, target);
    this._domModel = WebInspector.DOMModel.fromTarget(target);
    this._agent = target.cssAgent();
    this._styleLoader = new WebInspector.CSSStyleModel.ComputedStyleLoader(this);
    target.resourceTreeModel.addEventListener(WebInspector.ResourceTreeModel.EventTypes.MainFrameNavigated, this._mainFrameNavigated, this);
    target.registerCSSDispatcher(new WebInspector.CSSDispatcher(this));
    this._agent.enable().then(this._wasEnabled.bind(this));
    /** @type {!Map.<string, !WebInspector.CSSStyleSheetHeader>} */
    this._styleSheetIdToHeader = new Map();
    /** @type {!Map.<string, !Object.<!PageAgent.FrameId, !Array.<!CSSAgent.StyleSheetId>>>} */
    this._styleSheetIdsForURL = new Map();
}

/**
 * @param {!WebInspector.CSSStyleModel} cssModel
 * @param {!Array.<!CSSAgent.RuleMatch>|undefined} matchArray
 * @return {!Array.<!WebInspector.CSSRule>}
 */
WebInspector.CSSStyleModel.parseRuleMatchArrayPayload = function(cssModel, matchArray)
{
    if (!matchArray)
        return [];

    var result = [];
    for (var i = 0; i < matchArray.length; ++i)
        result.push(new WebInspector.CSSRule(cssModel, matchArray[i].rule, matchArray[i].matchingSelectors));
    return result;
}
WebInspector.CSSStyleModel.Events = {
    LayoutEditorChange: "LayoutEditorChange",
    MediaQueryResultChanged: "MediaQueryResultChanged",
    ModelWasEnabled: "ModelWasEnabled",
    PseudoStateForced: "PseudoStateForced",
    StyleSheetAdded: "StyleSheetAdded",
    StyleSheetChanged: "StyleSheetChanged",
    StyleSheetRemoved: "StyleSheetRemoved"
}

WebInspector.CSSStyleModel.MediaTypes = ["all", "braille", "embossed", "handheld", "print", "projection", "screen", "speech", "tty", "tv"];

WebInspector.CSSStyleModel.PseudoStateMarker = "pseudo-state-marker";

WebInspector.CSSStyleModel.prototype = {
    /**
     * @return {!Promise.<!Array.<!WebInspector.CSSMedia>>}
     */
    mediaQueriesPromise: function()
    {
        /**
         * @param {?Protocol.Error} error
         * @param {?Array.<!CSSAgent.CSSMedia>} payload
         * @return {!Array.<!WebInspector.CSSMedia>}
         * @this {!WebInspector.CSSStyleModel}
         */
        function parsePayload(error, payload)
        {
            return !error && payload ? WebInspector.CSSMedia.parseMediaArrayPayload(this, payload) : [];
        }

        return this._agent.getMediaQueries(parsePayload.bind(this));
    },

    /**
     * @return {boolean}
     */
    isEnabled: function()
    {
        return this._isEnabled;
    },

    /**
     * @param {?Protocol.Error} error
     */
    _wasEnabled: function(error)
    {
        if (error) {
            console.error("Failed to enabled CSS agent: " + error);
            return;
        }
        this._isEnabled = true;
        this.dispatchEventToListeners(WebInspector.CSSStyleModel.Events.ModelWasEnabled);
    },

    /**
     * @param {!DOMAgent.NodeId} nodeId
     * @return {!Promise.<?WebInspector.CSSStyleModel.MatchedStyleResult>}
     */
    matchedStylesPromise: function(nodeId)
    {
        /**
         * @param {?Protocol.Error} error
         * @param {?CSSAgent.CSSStyle=} inlinePayload
         * @param {?CSSAgent.CSSStyle=} attributesPayload
         * @param {!Array.<!CSSAgent.RuleMatch>=} matchedPayload
         * @param {!Array.<!CSSAgent.PseudoElementMatches>=} pseudoPayload
         * @param {!Array.<!CSSAgent.InheritedStyleEntry>=} inheritedPayload
         * @return {?WebInspector.CSSStyleModel.MatchedStyleResult}
         * @this {WebInspector.CSSStyleModel}
         */
        function callback(error, inlinePayload, attributesPayload, matchedPayload, pseudoPayload, inheritedPayload)
        {
            if (error)
                return null;

             var node = this._domModel.nodeForId(nodeId);
             if (!node)
                return null;

            return new WebInspector.CSSStyleModel.MatchedStyleResult(this, node, inlinePayload, attributesPayload, matchedPayload, pseudoPayload, inheritedPayload);
        }

        return this._agent.getMatchedStylesForNode(nodeId, callback.bind(this));
    },

    /**
     * @param {!DOMAgent.NodeId} nodeId
     * @return {!Promise.<?Map.<string, string>>}
     */
    computedStylePromise: function(nodeId)
    {
        return this._styleLoader.computedStylePromise(nodeId);
    },

    /**
     * @param {number} nodeId
     * @return {!Promise<?Array<string>>}
     */
    backgroundColorsPromise: function(nodeId)
    {
        /**
         * @param {?string} error
         * @param {!Array<string>=} backgroundColors
         * @return {?Array<string>}
         */
        function backgroundColorsCallback(error, backgroundColors) {
            return !error && backgroundColors ? backgroundColors : null;
        }
        return this._agent.getBackgroundColors(nodeId, backgroundColorsCallback);
    },

    /**
     * @param {number} nodeId
     * @return {!Promise.<?Array.<!CSSAgent.PlatformFontUsage>>}
     */
    platformFontsPromise: function(nodeId)
    {
        /**
         * @param {?Protocol.Error} error
         * @param {?Array.<!CSSAgent.PlatformFontUsage>} fonts
         * @return {?Array.<!CSSAgent.PlatformFontUsage>}
         */
        function platformFontsCallback(error, fonts)
        {
            return !error && fonts ? fonts : null;
        }

        return this._agent.getPlatformFontsForNode(nodeId, platformFontsCallback);
    },

    /**
     * @return {!Array.<!WebInspector.CSSStyleSheetHeader>}
     */
    allStyleSheets: function()
    {
        var values = this._styleSheetIdToHeader.valuesArray();
        /**
         * @param {!WebInspector.CSSStyleSheetHeader} a
         * @param {!WebInspector.CSSStyleSheetHeader} b
         * @return {number}
         */
        function styleSheetComparator(a, b)
        {
            if (a.sourceURL < b.sourceURL)
                return -1;
            else if (a.sourceURL > b.sourceURL)
                return 1;
            return a.startLine - b.startLine || a.startColumn - b.startColumn;
        }
        values.sort(styleSheetComparator);

        return values;
    },

    /**
     * @param {!DOMAgent.NodeId} nodeId
     * @return {!Promise.<?WebInspector.CSSStyleModel.InlineStyleResult>}
     */
    inlineStylesPromise: function(nodeId)
    {
        /**
         * @param {?Protocol.Error} error
         * @param {?CSSAgent.CSSStyle=} inlinePayload
         * @param {?CSSAgent.CSSStyle=} attributesStylePayload
         * @return {?WebInspector.CSSStyleModel.InlineStyleResult}
         * @this {WebInspector.CSSStyleModel}
         */
        function callback(error, inlinePayload, attributesStylePayload)
        {
            if (error || !inlinePayload)
                return null;
            var inlineStyle = inlinePayload ? new WebInspector.CSSStyleDeclaration(this, null, inlinePayload, WebInspector.CSSStyleDeclaration.Type.Inline) : null;
            var attributesStyle = attributesStylePayload ? new WebInspector.CSSStyleDeclaration(this, null, attributesStylePayload, WebInspector.CSSStyleDeclaration.Type.Attributes) : null;
            return new WebInspector.CSSStyleModel.InlineStyleResult(inlineStyle, attributesStyle);
        }

        return this._agent.getInlineStylesForNode(nodeId, callback.bind(this));
    },

    /**
     * @param {!WebInspector.DOMNode} node
     * @param {string} pseudoClass
     * @param {boolean} enable
     * @return {boolean}
     */
    forcePseudoState: function(node, pseudoClass, enable)
    {
        var pseudoClasses = node.marker(WebInspector.CSSStyleModel.PseudoStateMarker) || [];
        if (enable) {
            if (pseudoClasses.indexOf(pseudoClass) >= 0)
                return false;
            pseudoClasses.push(pseudoClass);
            node.setMarker(WebInspector.CSSStyleModel.PseudoStateMarker, pseudoClasses);
        } else {
            if (pseudoClasses.indexOf(pseudoClass) < 0)
                return false;
            pseudoClasses.remove(pseudoClass);
            if (!pseudoClasses.length)
                node.setMarker(WebInspector.CSSStyleModel.PseudoStateMarker, null);
        }

        this._agent.forcePseudoState(node.id, pseudoClasses);
        this.dispatchEventToListeners(WebInspector.CSSStyleModel.Events.PseudoStateForced, { node: node, pseudoClass: pseudoClass, enable: enable });
        return true;
    },

    /**
     * @param {!WebInspector.DOMNode} node
     * @return {?Array<string>} state
     */
    pseudoState: function(node)
    {
        return node.marker(WebInspector.CSSStyleModel.PseudoStateMarker) || [];
    },

    /**
     * @param {!WebInspector.CSSMedia} media
     * @param {string} newMediaText
     * @param {function(?WebInspector.CSSMedia)} userCallback
     */
    setMediaText: function(media, newMediaText, userCallback)
    {
        /**
         * @param {?Protocol.Error} error
         * @param {!CSSAgent.CSSMedia} mediaPayload
         * @return {?WebInspector.CSSMedia}
         * @this {WebInspector.CSSStyleModel}
         */
        function parsePayload(error, mediaPayload)
        {
            if (!mediaPayload)
                return null;
            this._domModel.markUndoableState();
            this._fireStyleSheetChanged(media.parentStyleSheetId);
            return WebInspector.CSSMedia.parsePayload(this, mediaPayload);
        }

        console.assert(!!media.parentStyleSheetId);
        WebInspector.userMetrics.actionTaken(WebInspector.UserMetrics.Action.StyleRuleEdited);
        this._agent.setMediaText(media.parentStyleSheetId, media.range, newMediaText, parsePayload.bind(this))
            .catchException(null)
            .then(userCallback);
    },

    /**
     * @param {!DOMAgent.NodeId} nodeId
     * @param {!Array.<!WebInspector.CSSRuleSelector>} selectors
     * @return {!Promise<?Array<number>>}
     */
    _computeMatchingSelectors: function(nodeId, selectors)
    {
        var ownerDocumentId = this._ownerDocumentId(nodeId);
        if (!ownerDocumentId || !selectors)
            return Promise.resolve(/** @type {?Array<number>} */(null));
        var matchingSelectors = [];
        var allSelectorsBarrier = new CallbackBarrier();
        for (var i = 0; i < selectors.length; ++i) {
            var boundCallback = allSelectorsBarrier.createCallback(selectorQueried.bind(null, i, nodeId, matchingSelectors));
            this._domModel.querySelectorAll(ownerDocumentId, selectors[i].value, boundCallback);
        }
        return new Promise(promiseConstructor);

        /**
         * @param {function(!Array<number>)} resolve
         */
        function promiseConstructor(resolve)
        {
            allSelectorsBarrier.callWhenDone(function() {
                resolve(matchingSelectors);
            });
        }

        /**
         * @param {number} index
         * @param {!DOMAgent.NodeId} nodeId
         * @param {!Array.<number>} matchingSelectors
         * @param {!Array.<!DOMAgent.NodeId>=} matchingNodeIds
         */
        function selectorQueried(index, nodeId, matchingSelectors, matchingNodeIds)
        {
            if (!matchingNodeIds)
                return;
            if (matchingNodeIds.indexOf(nodeId) !== -1)
                matchingSelectors.push(index);
        }
    },

    /**
     * @param {!CSSAgent.StyleSheetId} styleSheetId
     * @param {!WebInspector.DOMNode} node
     * @param {string} ruleText
     * @param {!WebInspector.TextRange} ruleLocation
     * @param {function(?WebInspector.CSSRule)} userCallback
     */
    addRule: function(styleSheetId, node, ruleText, ruleLocation, userCallback)
    {
        this._agent.addRule(styleSheetId, ruleText, ruleLocation, parsePayload.bind(this))
            .then(onRuleParsed.bind(this))
            .catchException(null)
            .then(userCallback);

        /**
         * @param {?Protocol.Error} error
         * @param {?CSSAgent.CSSRule} rulePayload
         * @return {?WebInspector.CSSRule}
         * @this {WebInspector.CSSStyleModel}
         */
        function parsePayload(error, rulePayload)
        {
            if (error || !rulePayload)
                return null;
            this._domModel.markUndoableState();
            this._fireStyleSheetChanged(styleSheetId);
            return new WebInspector.CSSRule(this, rulePayload);
        }

        /**
         * @param {?WebInspector.CSSRule} rule
         * @return {!Promise<?WebInspector.CSSRule>}
         * @this {WebInspector.CSSStyleModel}
         */
        function onRuleParsed(rule)
        {
            if (!rule)
                return Promise.resolve(/** @type {?WebInspector.CSSRule} */(null));

            return this._computeMatchingSelectors(node.id, rule.selectors)
                .then(updateMatchingSelectors.bind(null, rule));
        }

        /**
         * @param {!WebInspector.CSSRule} rule
         * @param {?Array<number>} matchingSelectors
         * @return {?WebInspector.CSSRule}
         */
        function updateMatchingSelectors(rule, matchingSelectors)
        {
            if (!matchingSelectors)
                return null;
            rule.matchingSelectors = matchingSelectors;
            return rule;
        }
    },

    /**
     * @param {!WebInspector.DOMNode} node
     * @param {function(?WebInspector.CSSStyleSheetHeader)} userCallback
     */
    requestViaInspectorStylesheet: function(node, userCallback)
    {
        var frameId = node.frameId() || this.target().resourceTreeModel.mainFrame.id;
        var headers = this._styleSheetIdToHeader.valuesArray();
        for (var i = 0; i < headers.length; ++i) {
            var styleSheetHeader = headers[i];
            if (styleSheetHeader.frameId === frameId && styleSheetHeader.isViaInspector()) {
                userCallback(styleSheetHeader);
                return;
            }
        }

        /**
         * @param {?Protocol.Error} error
         * @param {?CSSAgent.StyleSheetId} styleSheetId
         * @return {?WebInspector.CSSStyleSheetHeader}
         * @this {WebInspector.CSSStyleModel}
         */
        function innerCallback(error, styleSheetId)
        {
            return !error && styleSheetId ? this._styleSheetIdToHeader.get(styleSheetId) || null : null;
        }

        this._agent.createStyleSheet(frameId, innerCallback.bind(this))
            .catchException(null)
            .then(userCallback)
    },

    mediaQueryResultChanged: function()
    {
        this.dispatchEventToListeners(WebInspector.CSSStyleModel.Events.MediaQueryResultChanged);
    },

    /**
     * @param {!CSSAgent.StyleSheetId} id
     * @return {?WebInspector.CSSStyleSheetHeader}
     */
    styleSheetHeaderForId: function(id)
    {
        return this._styleSheetIdToHeader.get(id) || null;
    },

    /**
     * @return {!Array.<!WebInspector.CSSStyleSheetHeader>}
     */
    styleSheetHeaders: function()
    {
        return this._styleSheetIdToHeader.valuesArray();
    },

    /**
     * @param {!DOMAgent.NodeId} nodeId
     * @return {?DOMAgent.NodeId}
     */
    _ownerDocumentId: function(nodeId)
    {
        var node = this._domModel.nodeForId(nodeId);
        if (!node)
            return null;
        return node.ownerDocument ? node.ownerDocument.id : null;
    },

    /**
     * @param {!CSSAgent.StyleSheetId} styleSheetId
     */
    _fireStyleSheetChanged: function(styleSheetId)
    {
        this.dispatchEventToListeners(WebInspector.CSSStyleModel.Events.StyleSheetChanged, { styleSheetId: styleSheetId });
    },

    /**
     * @param {!CSSAgent.CSSStyleSheetHeader} header
     */
    _styleSheetAdded: function(header)
    {
        console.assert(!this._styleSheetIdToHeader.get(header.styleSheetId));
        var styleSheetHeader = new WebInspector.CSSStyleSheetHeader(this, header);
        this._styleSheetIdToHeader.set(header.styleSheetId, styleSheetHeader);
        var url = styleSheetHeader.resourceURL();
        if (!this._styleSheetIdsForURL.get(url))
            this._styleSheetIdsForURL.set(url, {});
        var frameIdToStyleSheetIds = this._styleSheetIdsForURL.get(url);
        var styleSheetIds = frameIdToStyleSheetIds[styleSheetHeader.frameId];
        if (!styleSheetIds) {
            styleSheetIds = [];
            frameIdToStyleSheetIds[styleSheetHeader.frameId] = styleSheetIds;
        }
        styleSheetIds.push(styleSheetHeader.id);
        this.dispatchEventToListeners(WebInspector.CSSStyleModel.Events.StyleSheetAdded, styleSheetHeader);
    },

    /**
     * @param {!CSSAgent.StyleSheetId} id
     */
    _styleSheetRemoved: function(id)
    {
        var header = this._styleSheetIdToHeader.get(id);
        console.assert(header);
        if (!header)
            return;
        this._styleSheetIdToHeader.remove(id);
        var url = header.resourceURL();
        var frameIdToStyleSheetIds = /** @type {!Object.<!PageAgent.FrameId, !Array.<!CSSAgent.StyleSheetId>>} */ (this._styleSheetIdsForURL.get(url));
        console.assert(frameIdToStyleSheetIds, "No frameId to styleSheetId map is available for given style sheet URL.");
        frameIdToStyleSheetIds[header.frameId].remove(id);
        if (!frameIdToStyleSheetIds[header.frameId].length) {
            delete frameIdToStyleSheetIds[header.frameId];
            if (!Object.keys(frameIdToStyleSheetIds).length)
                this._styleSheetIdsForURL.remove(url);
        }
        this.dispatchEventToListeners(WebInspector.CSSStyleModel.Events.StyleSheetRemoved, header);
    },

    /**
     * @param {string} url
     * @return {!Array.<!CSSAgent.StyleSheetId>}
     */
    styleSheetIdsForURL: function(url)
    {
        var frameIdToStyleSheetIds = this._styleSheetIdsForURL.get(url);
        if (!frameIdToStyleSheetIds)
            return [];

        var result = [];
        for (var frameId in frameIdToStyleSheetIds)
            result = result.concat(frameIdToStyleSheetIds[frameId]);
        return result;
    },

    /**
     * @param {!CSSAgent.StyleSheetId} styleSheetId
     * @param {string} newText
     * @param {boolean} majorChange
     * @return {!Promise.<?Protocol.Error>}
     */
    setStyleSheetText: function(styleSheetId, newText, majorChange)
    {
        var header = this._styleSheetIdToHeader.get(styleSheetId);
        console.assert(header);
        return header._setContentPromise(newText).then(callback.bind(this));

        /**
         * @param {?Protocol.Error} error
         * @return {?Protocol.Error}
         * @this {WebInspector.CSSStyleModel}
         */
        function callback(error)
        {
            if (error)
                return error;

            if (majorChange)
                this._domModel.markUndoableState();
            this._fireStyleSheetChanged(styleSheetId);
            return null;
        }
    },

    _mainFrameNavigated: function()
    {
        this._resetStyleSheets();
    },

    _resetStyleSheets: function()
    {
        var headers = this._styleSheetIdToHeader.valuesArray();
        this._styleSheetIdsForURL.clear();
        this._styleSheetIdToHeader.clear();
        for (var i = 0; i < headers.length; ++i)
            this.dispatchEventToListeners(WebInspector.CSSStyleModel.Events.StyleSheetRemoved, headers[i]);
    },

    /**
     * @override
     * @return {!Promise}
     */
    suspendModel: function()
    {
        this._isEnabled = false;
        return this._agent.disable().then(this._resetStyleSheets.bind(this));
    },

    /**
     * @override
     * @return {!Promise}
     */
    resumeModel: function()
    {
        return this._agent.enable().then(this._wasEnabled.bind(this));
    },

    /**
     * @param {!CSSAgent.StyleSheetId} id
     * @param {!CSSAgent.SourceRange} range
     */
    _layoutEditorChange: function(id, range)
    {
        this.dispatchEventToListeners(WebInspector.CSSStyleModel.Events.LayoutEditorChange, {id: id, range: range});
    },

    /**
     * @param {number} nodeId
     * @param {string} name
     * @param {string} value
     */
    setEffectivePropertyValueForNode: function(nodeId, name, value)
    {
        this._agent.setEffectivePropertyValueForNode(nodeId, name, value);
    },

    __proto__: WebInspector.SDKModel.prototype
}

/**
 * @constructor
 * @extends {WebInspector.SDKObject}
 * @param {!WebInspector.CSSStyleModel} cssModel
 * @param {!CSSAgent.StyleSheetId} styleSheetId
 * @param {string} url
 * @param {number} lineNumber
 * @param {number=} columnNumber
 */
WebInspector.CSSLocation = function(cssModel, styleSheetId, url, lineNumber, columnNumber)
{
    WebInspector.SDKObject.call(this, cssModel.target());
    this._cssModel = cssModel;
    this.styleSheetId = styleSheetId;
    this.url = url;
    this.lineNumber = lineNumber;
    this.columnNumber = columnNumber || 0;
}

WebInspector.CSSLocation.prototype = {
    /**
     * @return {!WebInspector.CSSStyleModel}
     */
    cssModel: function()
    {
        return this._cssModel;
    },

    __proto__: WebInspector.SDKObject.prototype
}

/**
 * @constructor
 * @param {!WebInspector.CSSStyleModel} cssModel
 * @param {?WebInspector.CSSRule} parentRule
 * @param {!CSSAgent.CSSStyle} payload
 * @param {!WebInspector.CSSStyleDeclaration.Type} type
 */
WebInspector.CSSStyleDeclaration = function(cssModel, parentRule, payload, type)
{
    this._cssModel = cssModel;
    this.parentRule = parentRule;
    this._reinitialize(payload);
    this.type = type;
}

/** @enum {string} */
WebInspector.CSSStyleDeclaration.Type = {
    Regular: "Regular",
    Inline: "Inline",
    Attributes: "Attributes"
}

WebInspector.CSSStyleDeclaration.prototype = {
    /**
     * @param {!CSSAgent.CSSStyle} payload
     */
    _reinitialize: function(payload)
    {
        this.styleSheetId = payload.styleSheetId;
        this.range = payload.range ? WebInspector.TextRange.fromObject(payload.range) : null;

        var shorthandEntries = payload.shorthandEntries;
        /** @type {!Map.<string, string>} */
        this._shorthandValues = new Map();
        /** @type {!Set.<string>} */
        this._shorthandIsImportant = new Set();
        for (var i = 0; i < shorthandEntries.length; ++i) {
            this._shorthandValues.set(shorthandEntries[i].name, shorthandEntries[i].value);
            if (shorthandEntries[i].important)
                this._shorthandIsImportant.add(shorthandEntries[i].name);
        }

        this._allProperties = [];
        for (var i = 0; i < payload.cssProperties.length; ++i) {
            var property = WebInspector.CSSProperty.parsePayload(this, i, payload.cssProperties[i]);
            this._allProperties.push(property);
        }

        this._generateSyntheticPropertiesIfNeeded();
        this._computeInactiveProperties();

        this._activePropertyMap = new Map();
        for (var property of this._allProperties) {
            if (!property.activeInStyle())
                continue;
            this._activePropertyMap.set(property.name, property);
        }

        this.cssText = payload.cssText;
        this._leadingProperties = null;
    },

    _generateSyntheticPropertiesIfNeeded: function()
    {
        if (this.range)
            return;

        if (!this._shorthandValues.size)
            return;

        var propertiesSet = new Set();
        for (var property of this._allProperties)
            propertiesSet.add(property.name);

        var generatedProperties = [];
        // For style-based properties, generate shorthands with values when possible.
        for (var property of this._allProperties) {
            // For style-based properties, try generating shorthands.
            var shorthands = WebInspector.CSSMetadata.cssPropertiesMetainfo.shorthands(property.name) || [];
            for (var shorthand of shorthands) {
                if (propertiesSet.has(shorthand))
                    continue;  // There already is a shorthand this longhands falls under.
                var shorthandValue = this._shorthandValues.get(shorthand);
                if (!shorthandValue)
                    continue;  // Never generate synthetic shorthands when no value is available.

                // Generate synthetic shorthand we have a value for.
                var shorthandImportance = !!this._shorthandIsImportant.has(shorthand);
                var shorthandProperty = new WebInspector.CSSProperty(this, this.allProperties.length, shorthand, shorthandValue, shorthandImportance, false, true, false);
                generatedProperties.push(shorthandProperty);
                propertiesSet.add(shorthand);
            }
        }
        this._allProperties = this._allProperties.concat(generatedProperties);
    },

    /**
     * @return {!Array.<!WebInspector.CSSProperty>}
     */
    _computeLeadingProperties: function()
    {
        /**
         * @param {!WebInspector.CSSProperty} property
         * @return {boolean}
         */
        function propertyHasRange(property)
        {
            return !!property.range;
        }

        if (this.range)
            return this._allProperties.filter(propertyHasRange);

        var leadingProperties = [];
        for (var property of this._allProperties) {
            var shorthands = WebInspector.CSSMetadata.cssPropertiesMetainfo.shorthands(property.name) || [];
            var belongToAnyShorthand = false;
            for (var shorthand of shorthands) {
                if (this._shorthandValues.get(shorthand)) {
                    belongToAnyShorthand = true;
                    break;
                }
            }
            if (!belongToAnyShorthand)
                leadingProperties.push(property);
        }

        return leadingProperties;
    },

    /**
     * @return {!Array.<!WebInspector.CSSProperty>}
     */
    leadingProperties: function()
    {
        if (!this._leadingProperties)
            this._leadingProperties = this._computeLeadingProperties();
        return this._leadingProperties;
    },

    /**
     * @return {!WebInspector.Target}
     */
    target: function()
    {
        return this._cssModel.target();
    },

    /**
     * @return {!WebInspector.CSSStyleModel}
     */
    cssModel: function()
    {
        return this._cssModel;
    },

    /**
     * @param {string} styleSheetId
     * @param {!WebInspector.TextRange} oldRange
     * @param {!WebInspector.TextRange} newRange
     */
    sourceStyleSheetEdited: function(styleSheetId, oldRange, newRange)
    {
        if (this.styleSheetId !== styleSheetId)
            return;
        if (this.range)
            this.range = this.range.rebaseAfterTextEdit(oldRange, newRange);
        for (var i = 0; i < this._allProperties.length; ++i)
            this._allProperties[i].sourceStyleSheetEdited(styleSheetId, oldRange, newRange);
    },

    _computeInactiveProperties: function()
    {
        var activeProperties = {};
        for (var i = 0; i < this._allProperties.length; ++i) {
            var property = this._allProperties[i];
            if (property.disabled || !property.parsedOk) {
                property._setActive(false);
                continue;
            }
            var canonicalName = WebInspector.CSSMetadata.canonicalPropertyName(property.name);
            var activeProperty = activeProperties[canonicalName];
            if (!activeProperty) {
                activeProperties[canonicalName] = property;
            } else if (!activeProperty.important || property.important) {
                activeProperty._setActive(false);
                activeProperties[canonicalName] = property;
            } else {
                property._setActive(false);
            }
        }
    },

    get allProperties()
    {
        return this._allProperties;
    },

    /**
     * @param {string} name
     * @return {string}
     */
    getPropertyValue: function(name)
    {
        var property = this._activePropertyMap.get(name);
        return property ? property.value : "";
    },

    /**
     * @param {string} name
     * @return {boolean}
     */
    isPropertyImplicit: function(name)
    {
        var property = this._activePropertyMap.get(name);
        return property ? property.implicit : "";
    },

    /**
     * @param {string} name
     * @return {!Array.<!WebInspector.CSSProperty>}
     */
    longhandProperties: function(name)
    {
        var longhands = WebInspector.CSSMetadata.cssPropertiesMetainfo.longhands(name);
        var result = [];
        for (var i = 0; longhands && i < longhands.length; ++i) {
            var property = this._activePropertyMap.get(longhands[i]);
            if (property)
                result.push(property);
        }
        return result;
    },

    /**
     * @param {number} index
     * @return {?WebInspector.CSSProperty}
     */
    propertyAt: function(index)
    {
        return (index < this.allProperties.length) ? this.allProperties[index] : null;
    },

    /**
     * @return {number}
     */
    pastLastSourcePropertyIndex: function()
    {
        for (var i = this.allProperties.length - 1; i >= 0; --i) {
            if (this.allProperties[i].range)
                return i + 1;
        }
        return 0;
    },

    /**
     * @param {number} index
     * @return {!WebInspector.TextRange}
     */
    _insertionRange: function(index)
    {
        var property = this.propertyAt(index);
        return property && property.range ? property.range.collapseToStart() : this.range.collapseToEnd();
    },

    /**
     * @param {number=} index
     * @return {!WebInspector.CSSProperty}
     */
    newBlankProperty: function(index)
    {
        index = (typeof index === "undefined") ? this.pastLastSourcePropertyIndex() : index;
        var property = new WebInspector.CSSProperty(this, index, "", "", false, false, true, false, "", this._insertionRange(index));
        return property;
    },

    /**
     * @param {string} text
     * @param {boolean} majorChange
     * @return {!Promise.<boolean>}
     */
    setText: function(text, majorChange)
    {
        if (!this.styleSheetId)
            return Promise.resolve(false);

        /**
         * @param {?Protocol.Error} error
         * @param {?CSSAgent.CSSStyle} stylePayload
         * @return {boolean}
         * @this {WebInspector.CSSStyleDeclaration}
         */
        function parsePayload(error, stylePayload)
        {
            if (error || !stylePayload)
                return false;

            if (majorChange)
                this._cssModel._domModel.markUndoableState();
            this._reinitialize(stylePayload);
            this._cssModel._fireStyleSheetChanged(this.styleSheetId);
            return true;
        }

        return this._cssModel._agent.setStyleText(this.styleSheetId, this.range.serializeToObject(), text, parsePayload.bind(this))
            .catchException(false);
    },

    /**
     * @param {number} index
     * @param {string} name
     * @param {string} value
     * @param {function(boolean)=} userCallback
     */
    insertPropertyAt: function(index, name, value, userCallback)
    {
        this.newBlankProperty(index).setText(name + ": " + value + ";", false, true)
            .then(userCallback);
    },

    /**
     * @param {string} name
     * @param {string} value
     * @param {function(boolean)=} userCallback
     */
    appendProperty: function(name, value, userCallback)
    {
        this.insertPropertyAt(this.allProperties.length, name, value, userCallback);
    }
}

/**
 * @constructor
 * @param {!CSSAgent.Selector} payload
 */
WebInspector.CSSRuleSelector = function(payload)
{
    this.value = payload.value;
    if (payload.range)
        this.range = WebInspector.TextRange.fromObject(payload.range);
}

/**
 * @param {!CSSAgent.SelectorList} selectorList
 * @return {!Array<!WebInspector.CSSRuleSelector>}
 */
WebInspector.CSSRuleSelector.parseSelectorListPayload = function(selectorList)
{
    var selectors = [];
    for (var i = 0; i < selectorList.selectors.length; ++i) {
        var selectorPayload = selectorList.selectors[i];
        selectors.push(new WebInspector.CSSRuleSelector(selectorPayload));
    }
    return selectors;
}

WebInspector.CSSRuleSelector.prototype = {
    /**
     * @param {!WebInspector.TextRange} oldRange
     * @param {!WebInspector.TextRange} newRange
     */
    sourceStyleRuleEdited: function(oldRange, newRange)
    {
        if (!this.range)
            return;
        this.range = this.range.rebaseAfterTextEdit(oldRange, newRange);
    }
}

/**
 * @constructor
 * @param {!WebInspector.CSSStyleModel} cssModel
 * @param {!CSSAgent.CSSRule} payload
 * @param {!Array.<number>=} matchingSelectors
 */
WebInspector.CSSRule = function(cssModel, payload, matchingSelectors)
{
    this._cssModel = cssModel;
    this.styleSheetId = payload.styleSheetId;
    if (matchingSelectors)
        this.matchingSelectors = matchingSelectors;

    /** @type {!Array.<!WebInspector.CSSRuleSelector>} */
    this.selectors = WebInspector.CSSRuleSelector.parseSelectorListPayload(payload.selectorList);

    if (this.styleSheetId) {
        var styleSheetHeader = cssModel.styleSheetHeaderForId(this.styleSheetId);
        this.sourceURL = styleSheetHeader.sourceURL;
    }
    this.origin = payload.origin;
    this.style = new WebInspector.CSSStyleDeclaration(this._cssModel, this, payload.style, WebInspector.CSSStyleDeclaration.Type.Regular);
    if (payload.media)
        this.media = WebInspector.CSSMedia.parseMediaArrayPayload(cssModel, payload.media);
}

/**
 * @param {!WebInspector.CSSStyleModel} cssModel
 * @param {string} selectorText
 * @return {!WebInspector.CSSRule}
 */
WebInspector.CSSRule.createDummyRule = function(cssModel, selectorText)
{
    var dummyPayload = {
        selectorList: {
            selectors: [{ value: selectorText}],
        },
        style: {
            styleSheetId: "0",
            range: new WebInspector.TextRange(0, 0, 0, 0),
            shorthandEntries: [],
            cssProperties: []
        }
    };
    return new WebInspector.CSSRule(cssModel, /** @type {!CSSAgent.CSSRule} */(dummyPayload));
}

WebInspector.CSSRule.prototype = {
    /**
     * @param {!DOMAgent.NodeId} nodeId
     * @param {string} newSelector
     * @param {function(boolean)} userCallback
     */
    setSelectorText: function(nodeId, newSelector, userCallback)
    {
        /**
         * @param {?Protocol.Error} error
         * @param {?CSSAgent.SelectorList} selectorPayload
         * @return {?Array.<!WebInspector.CSSRuleSelector>}
         * @this {WebInspector.CSSRule}
         */
        function callback(error, selectorPayload)
        {
            if (error || !selectorPayload)
                return null;
            this._cssModel._domModel.markUndoableState();
            this._cssModel._fireStyleSheetChanged(/** @type {string} */(this.styleSheetId));
            return WebInspector.CSSRuleSelector.parseSelectorListPayload(selectorPayload);
        }

        if (!this.styleSheetId)
            throw "No rule stylesheet id";
        var range = this.selectorRange();
        if (!range)
            throw "Rule selector is not editable";
        WebInspector.userMetrics.actionTaken(WebInspector.UserMetrics.Action.StyleRuleEdited);
        this._cssModel._agent.setRuleSelector(this.styleSheetId, range, newSelector, callback.bind(this))
            .then(onNewSelectors.bind(this))
            .catchException(false)
            .then(userCallback);

        /**
         * @param {?Array<!WebInspector.CSSRuleSelector>} selectors
         * @return {!Promise<boolean>}
         * @this {WebInspector.CSSRule}
         */
        function onNewSelectors(selectors)
        {
            if (!selectors)
                return Promise.resolve(false);
            return this._cssModel._computeMatchingSelectors(nodeId, selectors)
                .then(onMatchingSelectors.bind(this, selectors));
        }

        /**
         * @param {!Array<!WebInspector.CSSRuleSelector>} selectors
         * @param {?Array<number>} matchingSelectors
         * @return {boolean}
         * @this {WebInspector.CSSRule}
         */
        function onMatchingSelectors(selectors, matchingSelectors)
        {
            if (!matchingSelectors)
                return false;
            this.selectors = selectors;
            this.matchingSelectors = matchingSelectors;
            return true;
        }
    },

    /**
     * @return {string}
     */
    selectorText: function()
    {
        return this.selectors.select("value").join(", ");
    },

    /**
     * @return {?WebInspector.TextRange}
     */
    selectorRange: function()
    {
        var firstRange = this.selectors[0].range;
        if (!firstRange)
            return null;
        var lastRange = this.selectors.peekLast().range;
        return new WebInspector.TextRange(firstRange.startLine, firstRange.startColumn, lastRange.endLine, lastRange.endColumn);
    },

    /**
     * @param {string} styleSheetId
     * @param {!WebInspector.TextRange} oldRange
     * @param {!WebInspector.TextRange} newRange
     */
    sourceStyleSheetEdited: function(styleSheetId, oldRange, newRange)
    {
        this._sourceStyleSheetEditedWithMedia(styleSheetId, oldRange, newRange, null, null);
    },

    /**
     * @param {string} styleSheetId
     * @param {!WebInspector.TextRange} oldRange
     * @param {!WebInspector.TextRange} newRange
     * @param {?WebInspector.CSSMedia} oldMedia
     * @param {?WebInspector.CSSMedia} newMedia
     */
    _sourceStyleSheetEditedWithMedia: function(styleSheetId, oldRange, newRange, oldMedia, newMedia)
    {
        if (this.styleSheetId === styleSheetId) {
            for (var i = 0; i < this.selectors.length; ++i)
                this.selectors[i].sourceStyleRuleEdited(oldRange, newRange);
        }
        if (this.media) {
            for (var i = 0; i < this.media.length; ++i) {
                if (oldMedia && newMedia && oldMedia.equal(this.media[i])) {
                    this.media[i] = newMedia;
                } else {
                    this.media[i].sourceStyleSheetEdited(styleSheetId, oldRange, newRange);
                }
            }
        }
        this.style.sourceStyleSheetEdited(styleSheetId, oldRange, newRange);
    },

    /**
     * @param {!WebInspector.CSSMedia} oldMedia
     * @param {!WebInspector.CSSMedia} newMedia
     */
    mediaEdited: function(oldMedia, newMedia)
    {
        this._sourceStyleSheetEditedWithMedia(/** @type {string} */ (oldMedia.parentStyleSheetId), oldMedia.range, newMedia.range, oldMedia, newMedia);
    },

    /**
     * @return {string}
     */
    resourceURL: function()
    {
        if (!this.styleSheetId)
            return "";
        var styleSheetHeader = this._cssModel.styleSheetHeaderForId(this.styleSheetId);
        return styleSheetHeader.resourceURL();
    },

    /**
     * @param {number} selectorIndex
     * @return {number}
     */
    lineNumberInSource: function(selectorIndex)
    {
        var selector = this.selectors[selectorIndex];
        if (!selector || !selector.range || !this.styleSheetId)
            return 0;
        var styleSheetHeader = this._cssModel.styleSheetHeaderForId(this.styleSheetId);
        return styleSheetHeader.lineNumberInSource(selector.range.startLine);
    },

    /**
     * @param {number} selectorIndex
     * @return {number|undefined}
     */
    columnNumberInSource: function(selectorIndex)
    {
        var selector = this.selectors[selectorIndex];
        if (!selector || !selector.range || !this.styleSheetId)
            return undefined;
        var styleSheetHeader = this._cssModel.styleSheetHeaderForId(this.styleSheetId);
        console.assert(styleSheetHeader);
        return styleSheetHeader.columnNumberInSource(selector.range.startLine, selector.range.startColumn);
    },

    /**
     * @return {boolean}
     */
    isUserAgent: function()
    {
        return this.origin === CSSAgent.StyleSheetOrigin.UserAgent;
    },

    /**
     * @return {boolean}
     */
    isInjected: function()
    {
        return this.origin === CSSAgent.StyleSheetOrigin.Injected;
    },

    /**
     * @return {boolean}
     */
    isViaInspector: function()
    {
        return this.origin === CSSAgent.StyleSheetOrigin.Inspector;
    },

    /**
     * @return {boolean}
     */
    isRegular: function()
    {
        return this.origin === CSSAgent.StyleSheetOrigin.Regular;
    }
}

/**
 * @constructor
 * @param {!WebInspector.CSSStyleDeclaration} ownerStyle
 * @param {number} index
 * @param {string} name
 * @param {string} value
 * @param {boolean} important
 * @param {boolean} disabled
 * @param {boolean} parsedOk
 * @param {boolean} implicit
 * @param {?string=} text
 * @param {!CSSAgent.SourceRange=} range
 */
WebInspector.CSSProperty = function(ownerStyle, index, name, value, important, disabled, parsedOk, implicit, text, range)
{
    this.ownerStyle = ownerStyle;
    this.index = index;
    this.name = name;
    this.value = value;
    this.important = important;
    this.disabled = disabled;
    this.parsedOk = parsedOk;
    this.implicit = implicit; // A longhand, implicitly set by missing values of shorthand.
    this.text = text;
    this.range = range ? WebInspector.TextRange.fromObject(range) : null;
    this._active = true;
}

/**
 * @param {!WebInspector.CSSStyleDeclaration} ownerStyle
 * @param {number} index
 * @param {!CSSAgent.CSSProperty} payload
 * @return {!WebInspector.CSSProperty}
 */
WebInspector.CSSProperty.parsePayload = function(ownerStyle, index, payload)
{
    // The following default field values are used in the payload:
    // important: false
    // parsedOk: true
    // implicit: false
    // disabled: false
    var result = new WebInspector.CSSProperty(
        ownerStyle, index, payload.name, payload.value, payload.important || false, payload.disabled || false, ("parsedOk" in payload) ? !!payload.parsedOk : true, !!payload.implicit, payload.text, payload.range);
    return result;
}

WebInspector.CSSProperty.prototype = {
    /**
     * @param {string} styleSheetId
     * @param {!WebInspector.TextRange} oldRange
     * @param {!WebInspector.TextRange} newRange
     */
    sourceStyleSheetEdited: function(styleSheetId, oldRange, newRange)
    {
        if (this.ownerStyle.styleSheetId !== styleSheetId)
            return;
        if (this.range)
            this.range = this.range.rebaseAfterTextEdit(oldRange, newRange);
    },

    /**
     * @param {boolean} active
     */
    _setActive: function(active)
    {
        this._active = active;
    },

    get propertyText()
    {
        if (this.text !== undefined)
            return this.text;

        if (this.name === "")
            return "";
        return this.name + ": " + this.value + (this.important ? " !important" : "") + ";";
    },

    /**
     * @return {boolean}
     */
    activeInStyle: function()
    {
        return this._active;
    },

    /**
     * @param {string} propertyText
     * @param {boolean} majorChange
     * @param {boolean} overwrite
     * @return {!Promise.<boolean>}
     */
    setText: function(propertyText, majorChange, overwrite)
    {
        if (!this.ownerStyle)
            return Promise.reject(new Error("No ownerStyle for property"));

        if (!this.ownerStyle.styleSheetId)
            return Promise.reject(new Error("No owner style id"));

        if (!this.range || !this.ownerStyle.range)
            return Promise.reject(new Error("Style not editable"));

        if (majorChange)
            WebInspector.userMetrics.actionTaken(WebInspector.UserMetrics.Action.StyleRuleEdited);

        if (overwrite && propertyText === this.propertyText) {
            if (majorChange)
                this.ownerStyle._cssModel._domModel.markUndoableState();
            return Promise.resolve(true);
        }

        var range = this.range.relativeTo(this.ownerStyle.range.startLine, this.ownerStyle.range.startColumn);
        var indentation = this.ownerStyle.cssText ? this._detectIndentation(this.ownerStyle.cssText) : WebInspector.moduleSetting("textEditorIndent").get();
        var endIndentation = this.ownerStyle.cssText ? indentation.substring(0, this.ownerStyle.range.endColumn) : "";
        var newStyleText = range.replaceInText(this.ownerStyle.cssText || "", String.sprintf(";%s;", propertyText));

        return self.runtime.instancePromise(WebInspector.TokenizerFactory)
            .then(this._formatStyle.bind(this, newStyleText, indentation, endIndentation))
            .then(setStyleText.bind(this));

        /**
         * @param {string} styleText
         * @this {WebInspector.CSSProperty}
         * @return {!Promise.<boolean>}
         */
        function setStyleText(styleText)
        {
            return this.ownerStyle.setText(styleText, majorChange);
        }
    },

    /**
     * @param {string} styleText
     * @param {string} indentation
     * @param {string} endIndentation
     * @param {!WebInspector.TokenizerFactory} tokenizerFactory
     * @return {string}
     */
    _formatStyle: function(styleText, indentation, endIndentation, tokenizerFactory)
    {
        var result = "";
        var lastWasSemicolon = true;
        var lastWasMeta = false;
        var insideProperty = false;
        var tokenize = tokenizerFactory.createTokenizer("text/css");

        tokenize("*{" + styleText + "}", processToken);

        return result + (indentation ? "\n" + endIndentation : "");

        /**
         * @param {string} token
         * @param {?string} tokenType
         * @param {number} column
         * @param {number} newColumn
         */
        function processToken(token, tokenType, column, newColumn)
        {
            if (token === "}" || token === ";")
                result = result.trimRight();  // collect trailing space before } and ;
            if (token === "}")
                return;
            if (newColumn <= 2)
                return;
            var isSemicolon = token === ";";
            if (isSemicolon && lastWasSemicolon)
                return;
            lastWasSemicolon = isSemicolon || (lastWasSemicolon && tokenType && tokenType.includes("css-comment")) || (lastWasSemicolon && !token.trim());

            // No formatting, only remove dupe ;
            if (!indentation) {
                result += token;
                return;
            }

            // Format line breaks.
            if (!insideProperty && !token.trim())
                return;
            if (tokenType && tokenType.includes("css-comment") && token.includes(":")) {
                result += "\n" + indentation + token;
                insideProperty = false;
                return;
            }

            if (isSemicolon)
                insideProperty = false;

            if (!insideProperty && tokenType && (tokenType.includes("css-meta") || (tokenType.includes("css-property") && !lastWasMeta))) {
                result += "\n" + indentation;
                insideProperty = true;
            }
            result += token;

            lastWasMeta = tokenType && tokenType.includes("css-meta");
        }
    },

    /**
     * @param {string} text
     * @return {string}
     */
    _detectIndentation: function(text)
    {
        var lines = text.split("\n");
        if (lines.length < 2)
            return "";
        return WebInspector.TextUtils.lineIndent(lines[1]);
    },

    /**
     * @param {string} newValue
     * @param {boolean} majorChange
     * @param {boolean} overwrite
     * @param {function(boolean)=} userCallback
     */
    setValue: function(newValue, majorChange, overwrite, userCallback)
    {
        var text = this.name + ": " + newValue + (this.important ? " !important" : "") + ";";
        this.setText(text, majorChange, overwrite).then(userCallback);
    },

    /**
     * @param {boolean} disabled
     * @return {!Promise.<boolean>}
     */
    setDisabled: function(disabled)
    {
        if (!this.ownerStyle)
            return Promise.resolve(false);
        if (disabled === this.disabled)
            return Promise.resolve(true);
        var propertyText = this.text.trim();
        var text = disabled ? "/* " + propertyText + " */" : this.text.substring(2, propertyText.length - 2).trim();
        return this.setText(text, true, true);
    }
}

/**
 * @constructor
 * @param {!CSSAgent.MediaQuery} payload
 */
WebInspector.CSSMediaQuery = function(payload)
{
    this._active = payload.active;
    this._expressions = [];
    for (var j = 0; j < payload.expressions.length; ++j)
        this._expressions.push(WebInspector.CSSMediaQueryExpression.parsePayload(payload.expressions[j]));
}

/**
 * @param {!CSSAgent.MediaQuery} payload
 * @return {!WebInspector.CSSMediaQuery}
 */
WebInspector.CSSMediaQuery.parsePayload = function(payload)
{
    return new WebInspector.CSSMediaQuery(payload);
}

WebInspector.CSSMediaQuery.prototype = {
    /**
     * @return {boolean}
     */
    active: function()
    {
        return this._active;
    },

    /**
     * @return {!Array.<!WebInspector.CSSMediaQueryExpression>}
     */
    expressions: function()
    {
        return this._expressions;
    }
}

/**
 * @constructor
 * @param {!CSSAgent.MediaQueryExpression} payload
 */
WebInspector.CSSMediaQueryExpression = function(payload)
{
    this._value = payload.value;
    this._unit = payload.unit;
    this._feature = payload.feature;
    this._valueRange = payload.valueRange ? WebInspector.TextRange.fromObject(payload.valueRange) : null;
    this._computedLength = payload.computedLength || null;
}

/**
 * @param {!CSSAgent.MediaQueryExpression} payload
 * @return {!WebInspector.CSSMediaQueryExpression}
 */
WebInspector.CSSMediaQueryExpression.parsePayload = function(payload)
{
    return new WebInspector.CSSMediaQueryExpression(payload);
}

WebInspector.CSSMediaQueryExpression.prototype = {
    /**
     * @return {number}
     */
    value: function()
    {
        return this._value;
    },

    /**
     * @return {string}
     */
    unit: function()
    {
        return this._unit;
    },

    /**
     * @return {string}
     */
    feature: function()
    {
        return this._feature;
    },

    /**
     * @return {?WebInspector.TextRange}
     */
    valueRange: function()
    {
        return this._valueRange;
    },

    /**
     * @return {?number}
     */
    computedLength: function()
    {
        return this._computedLength;
    }
}


/**
 * @constructor
 * @param {!WebInspector.CSSStyleModel} cssModel
 * @param {!CSSAgent.CSSMedia} payload
 */
WebInspector.CSSMedia = function(cssModel, payload)
{
    this._cssModel = cssModel;
    this.text = payload.text;
    this.source = payload.source;
    this.sourceURL = payload.sourceURL || "";
    this.range = payload.range ? WebInspector.TextRange.fromObject(payload.range) : null;
    this.parentStyleSheetId = payload.parentStyleSheetId;
    this.mediaList = null;
    if (payload.mediaList) {
        this.mediaList = [];
        for (var i = 0; i < payload.mediaList.length; ++i)
            this.mediaList.push(WebInspector.CSSMediaQuery.parsePayload(payload.mediaList[i]));
    }
}

WebInspector.CSSMedia.Source = {
    LINKED_SHEET: "linkedSheet",
    INLINE_SHEET: "inlineSheet",
    MEDIA_RULE: "mediaRule",
    IMPORT_RULE: "importRule"
};

/**
 * @param {!WebInspector.CSSStyleModel} cssModel
 * @param {!CSSAgent.CSSMedia} payload
 * @return {!WebInspector.CSSMedia}
 */
WebInspector.CSSMedia.parsePayload = function(cssModel, payload)
{
    return new WebInspector.CSSMedia(cssModel, payload);
}

/**
 * @param {!WebInspector.CSSStyleModel} cssModel
 * @param {!Array.<!CSSAgent.CSSMedia>} payload
 * @return {!Array.<!WebInspector.CSSMedia>}
 */
WebInspector.CSSMedia.parseMediaArrayPayload = function(cssModel, payload)
{
    var result = [];
    for (var i = 0; i < payload.length; ++i)
        result.push(WebInspector.CSSMedia.parsePayload(cssModel, payload[i]));
    return result;
}

WebInspector.CSSMedia.prototype = {
    /**
     * @param {string} styleSheetId
     * @param {!WebInspector.TextRange} oldRange
     * @param {!WebInspector.TextRange} newRange
     */
    sourceStyleSheetEdited: function(styleSheetId, oldRange, newRange)
    {
        if (this.parentStyleSheetId !== styleSheetId)
            return;
        if (this.range)
            this.range = this.range.rebaseAfterTextEdit(oldRange, newRange);
    },

    /**
     * @param {!WebInspector.CSSMedia} other
     * @return {boolean}
     */
    equal: function(other)
    {
        if (!this.parentStyleSheetId || !this.range || !other.range)
            return false;
        return  this.parentStyleSheetId === other.parentStyleSheetId && this.range.equal(other.range);
    },

    /**
     * @return {boolean}
     */
    active: function()
    {
        if (!this.mediaList)
            return true;
        for (var i = 0; i < this.mediaList.length; ++i) {
            if (this.mediaList[i].active())
                return true;
        }
        return false;
    },

    /**
     * @return {number|undefined}
     */
    lineNumberInSource: function()
    {
        if (!this.range)
            return undefined;
        var header = this.header();
        if (!header)
            return undefined;
        return header.lineNumberInSource(this.range.startLine);
    },

    /**
     * @return {number|undefined}
     */
    columnNumberInSource: function()
    {
        if (!this.range)
            return undefined;
        var header = this.header();
        if (!header)
            return undefined;
        return header.columnNumberInSource(this.range.startLine, this.range.startColumn);
    },

    /**
     * @return {?WebInspector.CSSStyleSheetHeader}
     */
    header: function()
    {
        return this.parentStyleSheetId ? this._cssModel.styleSheetHeaderForId(this.parentStyleSheetId) : null;
    },

    /**
     * @return {?WebInspector.CSSLocation}
     */
    rawLocation: function()
    {
        if (!this.header() || this.lineNumberInSource() === undefined)
            return null;
        var lineNumber = Number(this.lineNumberInSource());
        return new WebInspector.CSSLocation(this._cssModel, this.header().id, this.sourceURL, lineNumber, this.columnNumberInSource());
    }
}

/**
 * @constructor
 * @implements {WebInspector.ContentProvider}
 * @param {!WebInspector.CSSStyleModel} cssModel
 * @param {!CSSAgent.CSSStyleSheetHeader} payload
 */
WebInspector.CSSStyleSheetHeader = function(cssModel, payload)
{
    this._cssModel = cssModel;
    this.id = payload.styleSheetId;
    this.frameId = payload.frameId;
    this.sourceURL = payload.sourceURL;
    this.hasSourceURL = !!payload.hasSourceURL;
    this.sourceMapURL = payload.sourceMapURL;
    this.origin = payload.origin;
    this.title = payload.title;
    this.disabled = payload.disabled;
    this.isInline = payload.isInline;
    this.startLine = payload.startLine;
    this.startColumn = payload.startColumn;
    if (payload.ownerNode)
        this.ownerNode = new WebInspector.DeferredDOMNode(cssModel.target(), payload.ownerNode);
}

WebInspector.CSSStyleSheetHeader.prototype = {
    /**
     * @return {!WebInspector.Target}
     */
    target: function()
    {
        return this._cssModel.target();
    },

    /**
     * @return {!WebInspector.CSSStyleModel}
     */
    cssModel: function()
    {
        return this._cssModel;
    },

    /**
     * @return {string}
     */
    resourceURL: function()
    {
        return this.isViaInspector() ? this._viaInspectorResourceURL() : this.sourceURL;
    },

    /**
     * @return {string}
     */
    _viaInspectorResourceURL: function()
    {
        var frame = this._cssModel.target().resourceTreeModel.frameForId(this.frameId);
        console.assert(frame);
        var parsedURL = new WebInspector.ParsedURL(frame.url);
        var fakeURL = "inspector://" + parsedURL.host + parsedURL.folderPathComponents;
        if (!fakeURL.endsWith("/"))
            fakeURL += "/";
        fakeURL += "inspector-stylesheet";
        return fakeURL;
    },

    /**
     * @param {number} lineNumberInStyleSheet
     * @return {number}
     */
    lineNumberInSource: function(lineNumberInStyleSheet)
    {
        return this.startLine + lineNumberInStyleSheet;
    },

    /**
     * @param {number} lineNumberInStyleSheet
     * @param {number} columnNumberInStyleSheet
     * @return {number|undefined}
     */
    columnNumberInSource: function(lineNumberInStyleSheet, columnNumberInStyleSheet)
    {
        return (lineNumberInStyleSheet ? 0 : this.startColumn) + columnNumberInStyleSheet;
    },

    /**
     * @override
     * @return {string}
     */
    contentURL: function()
    {
        return this.resourceURL();
    },

    /**
     * @override
     * @return {!WebInspector.ResourceType}
     */
    contentType: function()
    {
        return WebInspector.resourceTypes.Stylesheet;
    },

    /**
     * @param {string} text
     * @return {string}
     */
    _trimSourceURL: function(text)
    {
        var sourceURLRegex = /\n[\040\t]*\/\*[#@][\040\t]sourceURL=[\040\t]*([^\s]*)[\040\t]*\*\/[\040\t]*$/mg;
        return text.replace(sourceURLRegex, "");
    },

    /**
     * @override
     * @param {function(string)} userCallback
     */
    requestContent: function(userCallback)
    {
        this._cssModel._agent.getStyleSheetText(this.id, textCallback.bind(this))
            .catchException("")
            .then(userCallback)

        /**
         * @param {?Protocol.Error} error
         * @param {?string} text
         * @return {string}
         * @this {WebInspector.CSSStyleSheetHeader}
         */
        function textCallback(error, text)
        {
            if (error || text === null) {
                WebInspector.console.error("Failed to get text for stylesheet " + this.id + ": " + error)
                text = "";
                // Fall through.
            }
            return this._trimSourceURL(text);
        }
    },

    /**
     * @override
     */
    searchInContent: function(query, caseSensitive, isRegex, callback)
    {
        function performSearch(content)
        {
            callback(WebInspector.ContentProvider.performSearchInContent(content, query, caseSensitive, isRegex));
        }

        // searchInContent should call back later.
        this.requestContent(performSearch);
    },

    /**
     * @param {string} newText
     * @return {!Promise.<?Protocol.Error>}
     */
    _setContentPromise: function(newText)
    {
        newText = this._trimSourceURL(newText);
        if (this.hasSourceURL)
            newText += "\n/*# sourceURL=" + this.sourceURL + " */";
        return this._cssModel._agent.setStyleSheetText(this.id, newText, callback.bind(this));

        /**
         * @param {?Protocol.Error} error
         * @param {string=} sourceMapURL
         * @return {?Protocol.Error}
         * @this {WebInspector.CSSStyleSheetHeader}
         */
        function callback(error, sourceMapURL)
        {
            this.sourceMapURL = sourceMapURL;
            return error || null;
        }
    },

    /**
     * @param {string} newText
     * @param {function(?Protocol.Error)} callback
     */
    setContent: function(newText, callback)
    {
        this._setContentPromise(newText)
            .catchException(null)
            .then(callback);
    },

    /**
     * @return {boolean}
     */
    isViaInspector: function()
    {
        return this.origin === "inspector";
    }
}

/**
 * @constructor
 * @implements {CSSAgent.Dispatcher}
 * @param {!WebInspector.CSSStyleModel} cssModel
 */
WebInspector.CSSDispatcher = function(cssModel)
{
    this._cssModel = cssModel;
}

WebInspector.CSSDispatcher.prototype = {
    /**
     * @override
     */
    mediaQueryResultChanged: function()
    {
        this._cssModel.mediaQueryResultChanged();
    },

    /**
     * @override
     * @param {!CSSAgent.StyleSheetId} styleSheetId
     */
    styleSheetChanged: function(styleSheetId)
    {
        this._cssModel._fireStyleSheetChanged(styleSheetId);
    },

    /**
     * @override
     * @param {!CSSAgent.CSSStyleSheetHeader} header
     */
    styleSheetAdded: function(header)
    {
        this._cssModel._styleSheetAdded(header);
    },

    /**
     * @override
     * @param {!CSSAgent.StyleSheetId} id
     */
    styleSheetRemoved: function(id)
    {
        this._cssModel._styleSheetRemoved(id);
    },

    /**
     * @override
     * @param {!CSSAgent.StyleSheetId} id
     * @param {!CSSAgent.SourceRange} range
     */
    layoutEditorChange: function(id, range)
    {
        this._cssModel._layoutEditorChange(id, range);
    },
}

/**
 * @constructor
 * @param {!WebInspector.CSSStyleModel} cssModel
 */
WebInspector.CSSStyleModel.ComputedStyleLoader = function(cssModel)
{
    this._cssModel = cssModel;
    /** @type {!Map.<!DOMAgent.NodeId, !Promise.<?WebInspector.CSSStyleDeclaration>>} */
    this._nodeIdToPromise = new Map();
}

WebInspector.CSSStyleModel.ComputedStyleLoader.prototype = {
    /**
     * @param {!DOMAgent.NodeId} nodeId
     * @return {!Promise.<?Map.<string, string>>}
     */
    computedStylePromise: function(nodeId)
    {
        if (!this._nodeIdToPromise[nodeId])
            this._nodeIdToPromise[nodeId] = this._cssModel._agent.getComputedStyleForNode(nodeId, parsePayload).then(cleanUp.bind(this));

        return this._nodeIdToPromise[nodeId];

        /**
         * @param {?Protocol.Error} error
         * @param {!Array.<!CSSAgent.CSSComputedStyleProperty>} computedPayload
         * @return {?Map.<string, string>}
         */
        function parsePayload(error, computedPayload)
        {
            if (error || !computedPayload)
                return null;
            var result = new Map();
            for (var property of computedPayload)
                result.set(property.name, property.value);
            return result;
        }

        /**
         * @param {?Map.<string, string>} computedStyle
         * @return {?Map.<string, string>}
         * @this {WebInspector.CSSStyleModel.ComputedStyleLoader}
         */
        function cleanUp(computedStyle)
        {
            delete this._nodeIdToPromise[nodeId];
            return computedStyle;
        }
    }
}

/**
 * @param {!WebInspector.Target} target
 * @return {?WebInspector.CSSStyleModel}
 */
WebInspector.CSSStyleModel.fromTarget = function(target)
{
    if (!target.isPage())
        return null;
    return /** @type {?WebInspector.CSSStyleModel} */ (target.model(WebInspector.CSSStyleModel));
}

/**
 * @param {!WebInspector.DOMNode} node
 * @return {!WebInspector.CSSStyleModel}
 */
WebInspector.CSSStyleModel.fromNode = function(node)
{
    return /** @type {!WebInspector.CSSStyleModel} */ (WebInspector.CSSStyleModel.fromTarget(node.target()));
}

/**
 * @constructor
 * @param {!WebInspector.CSSStyleModel} cssModel
 * @param {!WebInspector.DOMNode} node
 * @param {?CSSAgent.CSSStyle=} inlinePayload
 * @param {?CSSAgent.CSSStyle=} attributesPayload
 * @param {!Array.<!CSSAgent.RuleMatch>=} matchedPayload
 * @param {!Array.<!CSSAgent.PseudoElementMatches>=} pseudoPayload
 * @param {!Array.<!CSSAgent.InheritedStyleEntry>=} inheritedPayload
 */
WebInspector.CSSStyleModel.MatchedStyleResult = function(cssModel, node, inlinePayload, attributesPayload, matchedPayload, pseudoPayload, inheritedPayload)
{
    this._cssModel = cssModel;
    this._node = node;
    this._nodeStyles = [];
    this._nodeForStyle = new Map();
    this._inheritedStyles = new Set();

    /**
     * @this {WebInspector.CSSStyleModel.MatchedStyleResult}
     */
    function addAttributesStyle()
    {
        if (!attributesPayload)
            return;
        var style = new WebInspector.CSSStyleDeclaration(cssModel, null, attributesPayload, WebInspector.CSSStyleDeclaration.Type.Attributes);
        this._nodeForStyle.set(style, this._node);
        this._nodeStyles.push(style);
    }

    // Inline style has the greatest specificity.
    if (inlinePayload && this._node.nodeType() === Node.ELEMENT_NODE) {
        var style = new WebInspector.CSSStyleDeclaration(cssModel, null, inlinePayload, WebInspector.CSSStyleDeclaration.Type.Inline);
        this._nodeForStyle.set(style, this._node);
        this._nodeStyles.push(style);
    }

    // Add rules in reverse order to match the cascade order.
    var addedAttributesStyle;
    var matchedRules = WebInspector.CSSStyleModel.parseRuleMatchArrayPayload(cssModel, matchedPayload);
    for (var i = matchedRules.length - 1; i >= 0; --i) {
        var rule = matchedRules[i];
        if ((rule.isInjected() || rule.isUserAgent()) && !addedAttributesStyle) {
            // Show element's Style Attributes after all author rules.
            addedAttributesStyle = true;
            addAttributesStyle.call(this);
        }
        this._nodeForStyle.set(rule.style, this._node);
        this._nodeStyles.push(rule.style);
    }

    if (!addedAttributesStyle)
        addAttributesStyle.call(this);

    // Walk the node structure and identify styles with inherited properties.
    var parentNode = this._node.parentNode;
    for (var i = 0; inheritedPayload && i < inheritedPayload.length; ++i) {
        var entryPayload = inheritedPayload[i];
        var inheritedInlineStyle = entryPayload.inlineStyle ? new WebInspector.CSSStyleDeclaration(cssModel, null, entryPayload.inlineStyle, WebInspector.CSSStyleDeclaration.Type.Inline) : null;
        var inheritedMatchedCSSRules = entryPayload.matchedCSSRules ? WebInspector.CSSStyleModel.parseRuleMatchArrayPayload(cssModel, entryPayload.matchedCSSRules) : null;
        if (inheritedInlineStyle && this._containsInherited(inheritedInlineStyle)) {
            this._nodeForStyle.set(inheritedInlineStyle, parentNode);
            this._nodeStyles.push(inheritedInlineStyle);
            this._inheritedStyles.add(inheritedInlineStyle);
        }

        for (var j = inheritedMatchedCSSRules.length - 1; j >= 0; --j) {
            var inheritedRule = inheritedMatchedCSSRules[j];
            if (!this._containsInherited(inheritedRule.style))
                continue;
            this._nodeForStyle.set(inheritedRule.style, parentNode);
            this._nodeStyles.push(inheritedRule.style);
            this._inheritedStyles.add(inheritedRule.style);
        }
        parentNode = parentNode.parentNode;
    }

    // Set up pseudo styles map.
    this._pseudoStyles = new Map();
    if (pseudoPayload) {
        for (var i = 0; i < pseudoPayload.length; ++i) {
            var entryPayload = pseudoPayload[i];
            var pseudoElement = this._node.pseudoElements().get(entryPayload.pseudoType);
            var pseudoStyles = [];
            var rules = WebInspector.CSSStyleModel.parseRuleMatchArrayPayload(cssModel, entryPayload.matches);
            for (var j = rules.length - 1; j >= 0; --j) {
                var pseudoRule = rules[j];
                pseudoStyles.push(pseudoRule.style);
                this._nodeForStyle.set(pseudoRule.style, pseudoElement);
            }
            this._pseudoStyles.set(entryPayload.pseudoType, pseudoStyles);
        }
    }

    this.resetActiveProperties();
}

WebInspector.CSSStyleModel.MatchedStyleResult.prototype = {
    /**
     * @return {!WebInspector.DOMNode}
     */
    node: function()
    {
        return this._node;
    },

    /**
     * @param {!WebInspector.CSSStyleDeclaration} style
     * @return {boolean}
     */
    hasMatchingSelectors: function(style)
    {
        return style.parentRule ? style.parentRule.matchingSelectors && style.parentRule.matchingSelectors.length > 0 && this.mediaMatches(style) : true;
    },

    /**
     * @param {!WebInspector.CSSStyleDeclaration} style
     * @return {boolean}
     */
    mediaMatches: function(style)
    {
        var media = style.parentRule ? style.parentRule.media : [];
        for (var i = 0; media && i < media.length; ++i) {
            if (!media[i].active())
                return false;
        }
        return true;
    },

    /**
     * @return {!Array<!WebInspector.CSSStyleDeclaration>}
     */
    nodeStyles: function()
    {
        return this._nodeStyles;
    },

    /**
     * @return {!Map.<!DOMAgent.PseudoType, !Array<!WebInspector.CSSStyleDeclaration>>}
     */
    pseudoStyles: function()
    {
        return this._pseudoStyles;
    },

    /**
     * @param {!WebInspector.CSSStyleDeclaration} style
     * @return {boolean}
     */
    _containsInherited: function(style)
    {
        var properties = style.allProperties;
        for (var i = 0; i < properties.length; ++i) {
            var property = properties[i];
            // Does this style contain non-overridden inherited property?
            if (property.activeInStyle() && WebInspector.CSSMetadata.isPropertyInherited(property.name))
                return true;
        }
        return false;
    },

    /**
     * @param {!WebInspector.CSSStyleDeclaration} style
     * @return {?WebInspector.DOMNode}
     */
    nodeForStyle: function(style)
    {
        return this._nodeForStyle.get(style) || null;
    },

    /**
     * @param {!WebInspector.CSSStyleDeclaration} style
     * @return {boolean}
     */
    isInherited: function(style)
    {
        return this._inheritedStyles.has(style);
    },

    /**
     * @param {!WebInspector.CSSProperty} property
     * @return {?WebInspector.CSSStyleModel.MatchedStyleResult.PropertyState}
     */
    propertyState: function(property)
    {
        if (this._propertiesState.size === 0) {
            this._computeActiveProperties(this._nodeStyles, this._propertiesState);
            for (var pseudoElementStyles of this._pseudoStyles.valuesArray())
                this._computeActiveProperties(pseudoElementStyles, this._propertiesState);
        }
        return this._propertiesState.get(property) || null;
    },

    resetActiveProperties: function()
    {
        /** @type {!Map<!WebInspector.CSSProperty, !WebInspector.CSSStyleModel.MatchedStyleResult.PropertyState>} */
        this._propertiesState = new Map();
    },

    /**
     * @param {!Array<!WebInspector.CSSStyleDeclaration>} styles
     * @param {!Map<!WebInspector.CSSProperty, !WebInspector.CSSStyleModel.MatchedStyleResult.PropertyState>} result
     */
    _computeActiveProperties: function(styles, result)
    {
        /** @type {!Set.<string>} */
        var foundImportantProperties = new Set();
        /** @type {!Map.<string, !Map<string, !WebInspector.CSSProperty>>} */
        var propertyToEffectiveRule = new Map();
        /** @type {!Map.<string, !WebInspector.DOMNode>} */
        var inheritedPropertyToNode = new Map();
        /** @type {!Set<string>} */
        var allUsedProperties = new Set();
        for (var i = 0; i < styles.length; ++i) {
            var style = styles[i];
            if (!this.hasMatchingSelectors(style))
                continue;

            /** @type {!Map<string, !WebInspector.CSSProperty>} */
            var styleActiveProperties = new Map();
            var allProperties = style.allProperties;
            for (var j = 0; j < allProperties.length; ++j) {
                var property = allProperties[j];

                // Do not pick non-inherited properties from inherited styles.
                var inherited = this.isInherited(style);
                if (inherited && !WebInspector.CSSMetadata.isPropertyInherited(property.name))
                    continue;

                if (!property.activeInStyle()) {
                    result.set(property, WebInspector.CSSStyleModel.MatchedStyleResult.PropertyState.Overloaded);
                    continue;
                }

                var canonicalName = WebInspector.CSSMetadata.canonicalPropertyName(property.name);
                if (foundImportantProperties.has(canonicalName)) {
                    result.set(property, WebInspector.CSSStyleModel.MatchedStyleResult.PropertyState.Overloaded);
                    continue;
                }

                if (!property.important && allUsedProperties.has(canonicalName)) {
                    result.set(property, WebInspector.CSSStyleModel.MatchedStyleResult.PropertyState.Overloaded);
                    continue;
                }

                var isKnownProperty = propertyToEffectiveRule.has(canonicalName);
                var inheritedFromNode = inherited ? this.nodeForStyle(style) : null;
                if (!isKnownProperty && inheritedFromNode && !inheritedPropertyToNode.has(canonicalName))
                    inheritedPropertyToNode.set(canonicalName, inheritedFromNode);

                if (property.important) {
                    if (inherited && isKnownProperty && inheritedFromNode !== inheritedPropertyToNode.get(canonicalName)) {
                        result.set(property, WebInspector.CSSStyleModel.MatchedStyleResult.PropertyState.Overloaded);
                        continue;
                    }

                    foundImportantProperties.add(canonicalName);
                    if (isKnownProperty) {
                        var overloaded = /** @type {!WebInspector.CSSProperty} */(propertyToEffectiveRule.get(canonicalName).get(canonicalName));
                        result.set(overloaded, WebInspector.CSSStyleModel.MatchedStyleResult.PropertyState.Overloaded);
                        propertyToEffectiveRule.get(canonicalName).delete(canonicalName);
                    }
                }

                styleActiveProperties.set(canonicalName, property);
                allUsedProperties.add(canonicalName);
                propertyToEffectiveRule.set(canonicalName, styleActiveProperties);
                result.set(property, WebInspector.CSSStyleModel.MatchedStyleResult.PropertyState.Active);
            }

            // If every longhand of the shorthand is not active, then the shorthand is not active too.
            for (var property of style.leadingProperties()) {
                var canonicalName = WebInspector.CSSMetadata.canonicalPropertyName(property.name);
                if (!styleActiveProperties.has(canonicalName))
                    continue;
                var longhands = style.longhandProperties(property.name);
                if (!longhands.length)
                    continue;
                var notUsed = true;
                for (var longhand of longhands) {
                    var longhandCanonicalName = WebInspector.CSSMetadata.canonicalPropertyName(longhand.name);
                    notUsed = notUsed && !styleActiveProperties.has(longhandCanonicalName);
                }
                if (!notUsed)
                    continue;
                styleActiveProperties.delete(canonicalName);
                allUsedProperties.delete(canonicalName);
                result.set(property, WebInspector.CSSStyleModel.MatchedStyleResult.PropertyState.Overloaded);
            }
        }
    }
}

/** @enum {string} */
WebInspector.CSSStyleModel.MatchedStyleResult.PropertyState = {
    Active: "Active",
    Overloaded: "Overloaded"
}

/**
 * @constructor
 * @param {?WebInspector.CSSStyleDeclaration} inlineStyle
 * @param {?WebInspector.CSSStyleDeclaration} attributesStyle
 */
WebInspector.CSSStyleModel.InlineStyleResult = function(inlineStyle, attributesStyle)
{
    this.inlineStyle = inlineStyle;
    this.attributesStyle = attributesStyle;
}

