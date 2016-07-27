/*
 * Copyright (C) 2009 Google Inc. All rights reserved.
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
 * @param {string} className
 * @param {!Element=} parentElement
 */
WebInspector.Toolbar = function(className, parentElement)
{
    /** @type {!Array.<!WebInspector.ToolbarItem>} */
    this._items = [];
    this.element = parentElement ? parentElement.createChild("div") : createElement("div");
    this.element.className = className;
    this.element.classList.add("toolbar");
    this._shadowRoot = WebInspector.createShadowRootWithCoreStyles(this.element, "ui/toolbar.css");
    this._contentElement = this._shadowRoot.createChild("div", "toolbar-shadow");
    this._contentElement.createChild("content");
}

WebInspector.Toolbar.prototype = {
    makeVertical: function()
    {
        this._contentElement.classList.add("vertical");
    },

    makeBlueOnHover: function()
    {
        this._contentElement.classList.add("blue-on-hover");
    },

    /**
     * @param {boolean} enabled
     */
    setEnabled: function(enabled)
    {
        for (var item of this._items)
            item.setEnabled(enabled);
    },

    /**
     * @param {!WebInspector.ToolbarItem} item
     */
    appendToolbarItem: function(item)
    {
        this._items.push(item);
        item._toolbar = this;
        this._contentElement.insertBefore(item.element, this._contentElement.lastChild);
        this._hideSeparatorDupes();
    },

    appendSeparator: function()
    {
        this.appendToolbarItem(new WebInspector.ToolbarSeparator());
    },

    removeToolbarItems: function()
    {
        for (var item of this._items)
            delete item._toolbar;
        this._items = [];
        this._contentElement.removeChildren();
        this._contentElement.createChild("content");
    },

    /**
     * @param {string} color
     */
    setColor: function(color)
    {
        var style = createElement("style");
        style.textContent = "button.toolbar-item .glyph { background-color: " + color + " !important }";
        this._shadowRoot.appendChild(style);
    },

    /**
     * @param {string} color
     */
    setToggledColor: function(color)
    {
        var style = createElement("style");
        style.textContent = "button.toolbar-item.toggled-on .glyph { background-color: " + color + " !important }";
        this._shadowRoot.appendChild(style);
    },

    _hideSeparatorDupes: function()
    {
        if (!this._items.length)
            return;
        // Don't hide first and last separators if they were added explicitly.
        var previousIsSeparator = false;
        var lastSeparator;
        var nonSeparatorVisible = false;
        for (var i = 0; i < this._items.length; ++i) {
            if (this._items[i] instanceof WebInspector.ToolbarSeparator) {
                this._items[i].setVisible(!previousIsSeparator);
                previousIsSeparator = true;
                lastSeparator = this._items[i];
                continue;
            }
            if (this._items[i].visible()) {
                previousIsSeparator = false;
                lastSeparator = null;
                nonSeparatorVisible = true;
            }
        }
        if (lastSeparator && lastSeparator !== this._items.peekLast())
            lastSeparator.setVisible(false);

        this.element.classList.toggle("hidden", lastSeparator && lastSeparator.visible() && !nonSeparatorVisible);
    }
}

/**
 * @constructor
 * @extends {WebInspector.Object}
 * @param {!Element} element
 */
WebInspector.ToolbarItem = function(element)
{
    this.element = element;
    this.element.classList.add("toolbar-item");
    this._enabled = true;
    this._visible = true;
    this.element.addEventListener("mouseenter", this._mouseEnter.bind(this), false);
    this.element.addEventListener("mouseleave", this._mouseLeave.bind(this), false);
}

WebInspector.ToolbarItem.prototype = {
    _mouseEnter: function()
    {
        this.element.classList.add("hover");
    },

    _mouseLeave: function()
    {
        this.element.classList.remove("hover");
    },

    /**
     * @param {boolean} value
     */
    setEnabled: function(value)
    {
        if (this._enabled === value)
            return;
        this._enabled = value;
        this._applyEnabledState();
    },

    _applyEnabledState: function()
    {
        this.element.disabled = !this._enabled;
    },

    /**
     * @return {boolean} x
     */
    visible: function()
    {
        return this._visible;
    },

    /**
     * @param {boolean} x
     */
    setVisible: function(x)
    {
        if (this._visible === x)
            return;
        this.element.classList.toggle("hidden", !x);
        this._visible = x;
        if (this._toolbar && !(this instanceof WebInspector.ToolbarSeparator))
            this._toolbar._hideSeparatorDupes();
    },

    __proto__: WebInspector.Object.prototype
}

/**
 * @constructor
 * @extends {WebInspector.ToolbarItem}
 * @param {string} text
 * @param {string=} className
 */
WebInspector.ToolbarText = function(text, className)
{
    WebInspector.ToolbarItem.call(this, createElementWithClass("span", "toolbar-text"));
    if (className)
        this.element.classList.add(className);
    this._textElement = this.element.createTextChild(text);
}

WebInspector.ToolbarText.prototype = {
    /**
     * @param {string} text
     */
    setText: function(text)
    {
        this._textElement.textContent = text;
    },

    showGlyph: function()
    {
        if (!this._glyphElement) {
            this.element.classList.add("toolbar-text-glyphed");
            this._glyphElement = createElementWithClass("div", "glyph toolbar-button-theme");
            this.element.insertBefore(this._glyphElement, this._textElement);
        }
    },

    makeDimmed: function()
    {
        this.element.classList.add("toolbar-text-dimmed");
    },

    __proto__: WebInspector.ToolbarItem.prototype
}

/**
 * @constructor
 * @extends {WebInspector.ToolbarItem}
 * @param {string=} placeholder
 * @param {number=} growFactor
 */
WebInspector.ToolbarInput = function(placeholder, growFactor)
{
    WebInspector.ToolbarItem.call(this, createElementWithClass("input", "toolbar-item"));
    this.element.addEventListener("input", this._onChangeCallback.bind(this), false);
    if (growFactor)
        this.element.style.flexGrow = growFactor;
    if (placeholder)
        this.element.setAttribute("placeholder", placeholder);
    this._value = "";
}

WebInspector.ToolbarInput.Event = {
    TextChanged: "TextChanged"
};

WebInspector.ToolbarInput.prototype = {
    /**
     * @param {string} value
     */
    setValue: function(value)
    {
        this._value = value;
        this.element.value = value;
    },

    /**
     * @return {string}
     */
    value: function()
    {
        return this.element.value;
    },

    _onChangeCallback: function()
    {
        this.dispatchEventToListeners(WebInspector.ToolbarInput.Event.TextChanged, this.element.value);
    },

    __proto__: WebInspector.ToolbarItem.prototype
}

/**
 * @constructor
 * @extends {WebInspector.ToolbarItem}
 * @param {string} className
 */
WebInspector.AbstractToolbarButton = function(className)
{
    WebInspector.ToolbarItem.call(this, createElementWithClass("button", className + " toolbar-item"));
    this.element.addEventListener("click", this._clicked.bind(this), false);
    this.element.addEventListener("mousedown", this._mouseDown.bind(this), false);
    this.element.addEventListener("mouseup", this._mouseUp.bind(this), false);
    this._longClickController = new WebInspector.LongClickController(this.element);
    this._longClickController.addEventListener(WebInspector.LongClickController.Events.LongClick, this._onLongClick.bind(this));
}

WebInspector.AbstractToolbarButton.prototype = {
    /**
     * @param {!WebInspector.Event} event
     */
    _onLongClick: function(event)
    {
        var nativeEvent = event.data;
        this.dispatchEventToListeners("longClickDown", nativeEvent);
    },

    /**
     * @param {!Event} event
     */
    _clicked: function(event)
    {
        this._longClickController.reset();
        var defaultPrevented = this.dispatchEventToListeners("click", event);
        event.consume(defaultPrevented);
    },

    /**
     * @param {!Event} event
     */
    _mouseDown: function(event)
    {
        this.dispatchEventToListeners("mousedown", event);
    },

    /**
     * @param {!Event} event
     */
    _mouseUp: function(event)
    {
        this.dispatchEventToListeners("mouseup", event);
    },

    /**
     * @override
     */
    _applyEnabledState: function()
    {
        this.element.disabled = !this._enabled;
        this._longClickController.reset();
    },

    /**
     * @return {boolean}
     */
    enabled: function()
    {
        throw "Not implemented";
    },

    /**
     * @return {string}
     */
    title: function()
    {
        throw "Not implemented";
    },

    /**
     * @param {string} title
     */
    setTitle: function(title)
    {
        throw "Not implemented";
    },

    /**
     * @return {string}
     */
    state: function()
    {
        throw "Not implemented";
    },

    /**
     * @param {string} x
     */
    setState: function(x)
    {
        throw "Not implemented";
    },

    /**
     * @return {boolean}
     */
    toggled: function()
    {
        throw "Not implemented";
    },

    /**
     * @param {boolean} x
     */
    setToggled: function(x)
    {
        throw "Not implemented";
    },

    makeLongClickEnabled: function()
    {
        this._longClickController.enable();
        this._longClickGlyph = this.element.createChild("div", "long-click-glyph toolbar-button-theme");
    },

    unmakeLongClickEnabled: function()
    {
        this._longClickController.disable();
        if (this._longClickGlyph)
            this.element.removeChild(this._longClickGlyph);
    },

    __proto__: WebInspector.ToolbarItem.prototype
}

/**
 * @constructor
 * @extends {WebInspector.AbstractToolbarButton}
 * @param {string} title
 * @param {string} className
 * @param {number=} states
 */
WebInspector.ToolbarButtonBase = function(title, className, states)
{
    WebInspector.AbstractToolbarButton.call(this, className);

    this._states = states || 2;
    if (states == 2)
        this._state = "off";
    else
        this._state = "0";

    this.setTitle(title);
}

WebInspector.ToolbarButtonBase.prototype = {
    /**
     * @override
     * @return {boolean}
     */
    enabled: function()
    {
        return this._enabled;
    },

    /**
     * @override
     * @return {string}
     */
    title: function()
    {
        return this._title;
    },

    /**
     * @override
     * @param {string} title
     */
    setTitle: function(title)
    {
        if (this._title === title)
            return;
        this._title = title;
        WebInspector.Tooltip.install(this.element, title);
    },

    /**
     * @override
     * @return {string}
     */
    state: function()
    {
        return this._state;
    },

    /**
     * @override
     * @param {string} x
     */
    setState: function(x)
    {
        if (this._state === x)
            return;

        this.element.classList.remove("toggled-" + this._state);
        this.element.classList.add("toggled-" + x);
        this._state = x;
    },

    /**
     * @override
     * @return {boolean}
     */
    toggled: function()
    {
        if (this._states !== 2)
            throw("Only used toggled when there are 2 states, otherwise, use state");
        return this.state() === "on";
    },

    /**
     * @override
     * @param {boolean} x
     */
    setToggled: function(x)
    {
        if (this._states !== 2)
            throw("Only used toggled when there are 2 states, otherwise, use state");
        this.setState(x ? "on" : "off");
    },

    __proto__: WebInspector.AbstractToolbarButton.prototype
}

/**
 * @constructor
 * @extends {WebInspector.AbstractToolbarButton}
 * @param {!WebInspector.Action} action
 */
WebInspector.ActionToolbarButton = function(action)
{
    this._action = action;
    WebInspector.AbstractToolbarButton.call(this, action.icon());
    this._glyphElement = this.element.createChild("div", "glyph toolbar-button-theme");
    action.addEventListener(WebInspector.Action.Events.Enabled, this._enabledStateChanged, this);
    action.addEventListener(WebInspector.Action.Events.StateChanged, this._stateChanged, this);
    action.addEventListener(WebInspector.Action.Events.TitleChanged, this._titleChanged, this);
    this._titleChanged();
}

WebInspector.ActionToolbarButton.prototype = {
    /**
     * @override
     * @return {boolean}
     */
    enabled: function()
    {
        return this._action.enabled();
    },

    /**
     * @override
     * @param {boolean} value
     */
    setEnabled: function(value)
    {
        this._action.setEnabled(value);
    },

    /**
     * @param {!WebInspector.Event} event
     */
    _enabledStateChanged: function(event)
    {
        var enabled = /** @type {boolean} */ (event.data);
        WebInspector.ToolbarButtonBase.prototype.setEnabled.call(this, enabled);
    },

    /**
     * @override
     * @param {!Event} event
     */
    _clicked: function(event)
    {
        this._longClickController.reset();
        this._action.execute();
    },

    /**
     * @override
     * @return {string}
     */
    title: function()
    {
        return this._action.title();
    },

    /**
     * @override
     * @param {string} title
     */
    setTitle: function(title)
    {
        this._action.setTitle(title);
    },

    _titleChanged: function()
    {
        WebInspector.Tooltip.install(this.element, this._action.title(), this._action.id());
    },

    /**
     * @override
     * @return {string}
     */
    state: function()
    {
        return this._action.state();
    },

    /**
     * @override
     * @param {string} x
     */
    setState: function(x)
    {
        this._action.setState(x);
    },

    /**
     * @param {!WebInspector.Event} event
     */
    _stateChanged: function(event)
    {
        var data = /** @type {!{oldState: string, newState: string}} */ (event.data);
        this.element.classList.remove("toggled-" + data.oldState);
        this.element.classList.add("toggled-" + data.newState);
    },

    /**
     * @override
     * @return {boolean}
     */
    toggled: function()
    {
        return this._action.toggled();
    },

    /**
     * @override
     * @param {boolean} x
     */
    setToggled: function(x)
    {
        this._action.setToggled(x);
    },

    /**
     * @param {?function():!Array.<!WebInspector.ToolbarButton>} buttonsProvider
     */
    setLongClickOptionsEnabled: function(buttonsProvider)
    {
        if (buttonsProvider) {
            if (!this._longClickOptionsData) {
                this.makeLongClickEnabled();

                var longClickDownListener = this._showOptions.bind(this);
                this.addEventListener("longClickDown", longClickDownListener, this);

                this._longClickOptionsData = {
                    longClickDownListener: longClickDownListener
                };
            }
            this._longClickOptionsData.buttonsProvider = buttonsProvider;
        } else {
            if (!this._longClickOptionsData)
                return;

            this.removeEventListener("longClickDown", this._longClickOptionsData.longClickDownListener, this);
            delete this._longClickOptionsData;

            this.unmakeLongClickEnabled();
        }
    },

    _showOptions: function()
    {
        var buttons = this._longClickOptionsData.buttonsProvider();
        var mainButtonClone = new WebInspector.ToolbarButton(this.title(), this._action.icon(), this._action.statesCount());
        mainButtonClone.addEventListener("click", clicked.bind(this));

        /**
         * @param {!WebInspector.Event} event
         * @this {WebInspector.ActionToolbarButton}
         */
        function clicked(event)
        {
            this._clicked(/** @type {!Event} */ (event.data));
        }

        mainButtonClone.setState(this.state());
        buttons.push(mainButtonClone);

        var document = this.element.ownerDocument;
        document.documentElement.addEventListener("mouseup", mouseUp, false);

        var optionsGlassPane = new WebInspector.GlassPane(document);
        var optionsBar = new WebInspector.Toolbar("fill", optionsGlassPane.element);
        optionsBar._contentElement.classList.add("floating");
        const buttonHeight = 26;

        var hostButtonPosition = this.element.totalOffset();

        var topNotBottom = hostButtonPosition.top + buttonHeight * buttons.length < document.documentElement.offsetHeight;

        if (topNotBottom)
            buttons = buttons.reverse();

        optionsBar.element.style.height = (buttonHeight * buttons.length) + "px";
        if (topNotBottom)
            optionsBar.element.style.top = (hostButtonPosition.top + 1) + "px";
        else
            optionsBar.element.style.top = (hostButtonPosition.top - (buttonHeight * (buttons.length - 1))) + "px";
        optionsBar.element.style.left = (hostButtonPosition.left + 1) + "px";

        for (var i = 0; i < buttons.length; ++i) {
            buttons[i].element.addEventListener("mousemove", mouseOver, false);
            buttons[i].element.addEventListener("mouseout", mouseOut, false);
            optionsBar.appendToolbarItem(buttons[i]);
        }
        var hostButtonIndex = topNotBottom ? 0 : buttons.length - 1;
        buttons[hostButtonIndex].element.classList.add("emulate-active");

        function mouseOver(e)
        {
            if (e.which !== 1)
                return;
            var buttonElement = e.target.enclosingNodeOrSelfWithClass("toolbar-item");
            buttonElement.classList.add("emulate-active");
        }

        function mouseOut(e)
        {
            if (e.which !== 1)
                return;
            var buttonElement = e.target.enclosingNodeOrSelfWithClass("toolbar-item");
            buttonElement.classList.remove("emulate-active");
        }

        function mouseUp(e)
        {
            if (e.which !== 1)
                return;
            optionsGlassPane.dispose();
            document.documentElement.removeEventListener("mouseup", mouseUp, false);

            for (var i = 0; i < buttons.length; ++i) {
                if (buttons[i].element.classList.contains("emulate-active")) {
                    buttons[i].element.classList.remove("emulate-active");
                    buttons[i]._clicked(e);
                    break;
                }
            }
        }
    },

    __proto__: WebInspector.AbstractToolbarButton.prototype
}

/**
 * @constructor
 * @extends {WebInspector.ToolbarButtonBase}
 * @param {string} title
 * @param {string} className
 * @param {number=} states
 */
WebInspector.ToolbarButton = function(title, className, states)
{
    WebInspector.ToolbarButtonBase.call(this, title, className, states);

    this._glyphElement = this.element.createChild("div", "glyph toolbar-button-theme");
}

WebInspector.ToolbarButton.prototype = {
    /**
     * @param {string} iconURL
     */
    setBackgroundImage: function(iconURL)
    {
        this.element.style.backgroundImage = "url(" + iconURL + ")";
        this._glyphElement.classList.add("hidden");
    },

    __proto__: WebInspector.ToolbarButtonBase.prototype
}

/**
 * @param {string} actionId
 * @return {!WebInspector.ActionToolbarButton}
 */
WebInspector.ToolbarButton.createActionButton = function(actionId)
{
    return new WebInspector.ActionToolbarButton(WebInspector.actionRegistry.action(actionId));
}

/**
 * @constructor
 * @extends {WebInspector.ToolbarButton}
 * @param {string} title
 * @param {string} className
 * @param {function(!WebInspector.ContextMenu)} contextMenuHandler
 */
WebInspector.ToolbarMenuButton = function(title, className, contextMenuHandler)
{
    WebInspector.ToolbarButton.call(this, title, className);
    this._contextMenuHandler = contextMenuHandler;
}

WebInspector.ToolbarMenuButton.prototype = {
    /**
     * @override
     * @param {!Event} event
     */
    _clicked: function(event)
    {
        var contextMenu = new WebInspector.ContextMenu(event,
            false,
            this.element.totalOffsetLeft(),
            this.element.totalOffsetTop() + this.element.offsetHeight);
        this._contextMenuHandler(contextMenu);
        contextMenu.show();
    },

    __proto__: WebInspector.ToolbarButton.prototype
}

/**
 * @constructor
 * @extends {WebInspector.ToolbarButton}
 * @param {!WebInspector.Setting} setting
 * @param {string} className
 * @param {string} title
 * @param {string=} toggledTitle
 */
WebInspector.ToolbarSettingToggle = function(setting, className, title, toggledTitle)
{
    WebInspector.ToolbarButton.call(this, "", className, 2);
    this._defaultTitle = title;
    this._toggledTitle = toggledTitle || title;
    this._setting = setting;
    this._settingChanged();
    this._setting.addChangeListener(this._settingChanged, this);
}

WebInspector.ToolbarSettingToggle.prototype = {
    _settingChanged: function()
    {
        var toggled = this._setting.get();
        this.setToggled(toggled);
        this.setTitle(toggled ? this._toggledTitle : this._defaultTitle);
    },

    /**
     * @override
     * @param {!Event} event
     */
    _clicked: function(event)
    {
        this._setting.set(!this.toggled());
        WebInspector.ToolbarButton.prototype._clicked.call(this, event);
    },

    __proto__: WebInspector.ToolbarButton.prototype
}

/**
 * @constructor
 * @extends {WebInspector.ToolbarItem}
 */
WebInspector.ToolbarSeparator = function()
{
    WebInspector.ToolbarItem.call(this, createElementWithClass("div", "toolbar-divider"));
}

WebInspector.ToolbarSeparator.prototype = {
    __proto__: WebInspector.ToolbarItem.prototype
}

/**
 * @constructor
 * @extends {WebInspector.ToolbarButtonBase}
 * @param {string} title
 * @param {string} className
 * @param {string} text
 * @param {number=} states
 */
WebInspector.ToolbarTextButton = function(title, className, text, states)
{
    WebInspector.ToolbarButtonBase.call(this, title, className, states);

    this._textElement = this.element.createChild("div", "toolbar-button-text");
    this._textElement.textContent = text;
}

WebInspector.ToolbarTextButton.prototype = {
    __proto__: WebInspector.ToolbarButtonBase.prototype
}

/**
 * @interface
 */
WebInspector.ToolbarItem.Provider = function()
{
}

WebInspector.ToolbarItem.Provider.prototype = {
    /**
     * @return {?WebInspector.ToolbarItem}
     */
    item: function() {}
}

/**
 * @constructor
 * @extends {WebInspector.ToolbarItem}
 * @param {?function(!Event)} changeHandler
 * @param {string=} className
 */
WebInspector.ToolbarComboBox = function(changeHandler, className)
{
    WebInspector.ToolbarItem.call(this, createElementWithClass("span", "toolbar-select-container"));

    this._selectElement = this.element.createChild("select", "toolbar-item");
    this.element.createChild("div", "toolbar-select-arrow");
    if (changeHandler)
        this._selectElement.addEventListener("change", changeHandler, false);
    if (className)
        this._selectElement.classList.add(className);
}

WebInspector.ToolbarComboBox.prototype = {
    /**
     * @return {!HTMLSelectElement}
     */
    selectElement: function()
    {
        return /** @type {!HTMLSelectElement} */ (this._selectElement);
    },

    /**
     * @return {number}
     */
    size: function()
    {
        return this._selectElement.childElementCount;
    },

    /**
     * @return {!Array.<!Element>}
     */
    options: function()
    {
        return Array.prototype.slice.call(this._selectElement.children, 0);
    },

    /**
     * @param {!Element} option
     */
    addOption: function(option)
    {
        this._selectElement.appendChild(option);
    },

    /**
     * @param {string} label
     * @param {string=} title
     * @param {string=} value
     * @return {!Element}
     */
    createOption: function(label, title, value)
    {
        var option = this._selectElement.createChild("option");
        option.text = label;
        if (title)
            option.title = title;
        if (typeof value !== "undefined")
            option.value = value;
        return option;
    },

    /**
     * @override
     */
    _applyEnabledState: function()
    {
        this._selectElement.disabled = !this._enabled;
    },

    /**
     * @param {!Element} option
     */
    removeOption: function(option)
    {
        this._selectElement.removeChild(option);
    },

    removeOptions: function()
    {
        this._selectElement.removeChildren();
    },

    /**
     * @return {?Element}
     */
    selectedOption: function()
    {
        if (this._selectElement.selectedIndex >= 0)
            return this._selectElement[this._selectElement.selectedIndex];
        return null;
    },

    /**
     * @param {!Element} option
     */
    select: function(option)
    {
        this._selectElement.selectedIndex = Array.prototype.indexOf.call(/** @type {?} */ (this._selectElement), option);
    },

    /**
     * @param {number} index
     */
    setSelectedIndex: function(index)
    {
        this._selectElement.selectedIndex = index;
    },

    /**
     * @return {number}
     */
    selectedIndex: function()
    {
        return this._selectElement.selectedIndex;
    },

    /**
     * @param {number} width
     */
    setMaxWidth: function(width)
    {
        this._selectElement.style.maxWidth = width + "px";
    },

    __proto__: WebInspector.ToolbarItem.prototype
}

/**
 * @constructor
 * @extends {WebInspector.ToolbarItem}
 * @param {string} text
 * @param {string=} title
 * @param {!WebInspector.Setting=} setting
 */
WebInspector.ToolbarCheckbox = function(text, title, setting)
{
    WebInspector.ToolbarItem.call(this, createCheckboxLabel(text));
    this.element.classList.add("checkbox");
    this.inputElement = this.element.checkboxElement;
    if (title)
        this.element.title = title;
    if (setting)
        WebInspector.SettingsUI.bindCheckbox(this.inputElement, setting);
}

WebInspector.ToolbarCheckbox.prototype = {
    /**
     * @return {boolean}
     */
    checked: function()
    {
        return this.inputElement.checked;
    },

    __proto__: WebInspector.ToolbarItem.prototype
}

/**
 * @constructor
 * @extends {WebInspector.Toolbar}
 * @param {string} location
 * @param {!Element=} parentElement
 */
WebInspector.ExtensibleToolbar = function(location, parentElement)
{
    WebInspector.Toolbar.call(this, "", parentElement);
    this._loadItems(location);
}

WebInspector.ExtensibleToolbar.prototype = {
    /**
     * @param {string} location
     */
    _loadItems: function(location)
    {
        var extensions = self.runtime.extensions(WebInspector.ToolbarItem.Provider);
        var promises = [];
        for (var i = 0; i < extensions.length; ++i) {
            if (extensions[i].descriptor()["location"] === location)
                promises.push(resolveItem(extensions[i]));
        }
        Promise.all(promises).then(appendItemsInOrder.bind(this));

        /**
         * @param {!Runtime.Extension} extension
         * @return {!Promise.<?WebInspector.ToolbarItem>}
         */
        function resolveItem(extension)
        {
            var descriptor = extension.descriptor();
            if (descriptor["separator"])
                return Promise.resolve(/** @type {?WebInspector.ToolbarItem} */(new WebInspector.ToolbarSeparator()));
            if (descriptor["actionId"])
                return Promise.resolve(/** @type {?WebInspector.ToolbarItem} */(WebInspector.ToolbarButton.createActionButton(descriptor["actionId"])));
            if (!descriptor["className"])
                return Promise.resolve(/** @type {?WebInspector.ToolbarItem} */(new WebInspector.ToolbarButton(WebInspector.UIString(descriptor["title"]), descriptor["elementClass"])));
            return extension.instancePromise().then(fetchItemFromProvider);

            /**
             * @param {!Object} provider
             */
            function fetchItemFromProvider(provider)
            {
                return /** @type {!WebInspector.ToolbarItem.Provider} */ (provider).item();
            }
        }

        /**
         * @param {!Array.<?WebInspector.ToolbarItem>} items
         * @this {WebInspector.ExtensibleToolbar}
         */
        function appendItemsInOrder(items)
        {
            for (var i = 0; i < items.length; ++i) {
                var item = items[i];
                if (item)
                    this.appendToolbarItem(item);
            }
        }
    },

    __proto__: WebInspector.Toolbar.prototype
}