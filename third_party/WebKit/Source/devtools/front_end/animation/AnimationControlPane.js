// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @constructor
 * @extends {WebInspector.ElementsPanel.BaseToolbarPaneWidget}
 */
WebInspector.AnimationControlPane = function(toolbarItem)
{
    WebInspector.ElementsPanel.BaseToolbarPaneWidget.call(this, toolbarItem);
    this._animationsPaused = false;
    this._animationsPlaybackRate = 1;

    this.element.className =  "styles-animations-controls-pane";
    this.element.createChild("div").createTextChild("Animations");
    var container = this.element.createChild("div", "animations-controls");

    var toolbar = new WebInspector.Toolbar("");
    this._animationsPauseButton = new WebInspector.ToolbarButton("", "pause-toolbar-item");
    toolbar.appendToolbarItem(this._animationsPauseButton);
    this._animationsPauseButton.addEventListener("click", this._pauseButtonHandler.bind(this));
    container.appendChild(toolbar.element);

    this._animationsPlaybackSlider = container.createChild("input");
    this._animationsPlaybackSlider.type = "range";
    this._animationsPlaybackSlider.min = 0;
    this._animationsPlaybackSlider.max = WebInspector.AnimationTimeline.GlobalPlaybackRates.length - 1;
    this._animationsPlaybackSlider.value = this._animationsPlaybackSlider.max;
    this._animationsPlaybackSlider.addEventListener("input", this._playbackSliderInputHandler.bind(this));

    this._animationsPlaybackLabel = container.createChild("div", "playback-label");
    this._animationsPlaybackLabel.createTextChild("1x");
}

WebInspector.AnimationControlPane.prototype = {
    /**
     * @param {!Event} event
     */
    _playbackSliderInputHandler: function (event)
    {
        this._animationsPlaybackRate = WebInspector.AnimationTimeline.GlobalPlaybackRates[event.target.value];
        WebInspector.AnimationModel.fromTarget(this._target).setPlaybackRate(this._animationsPaused ? 0 : this._animationsPlaybackRate);
        this._animationsPlaybackLabel.textContent = this._animationsPlaybackRate + "x";
        WebInspector.userMetrics.actionTaken(WebInspector.UserMetrics.Action.AnimationsPlaybackRateChanged);
    },

    _pauseButtonHandler: function ()
    {
        this._animationsPaused = !this._animationsPaused;
        WebInspector.AnimationModel.fromTarget(this._target).setPlaybackRate(this._animationsPaused ? 0 : this._animationsPlaybackRate);
        WebInspector.userMetrics.actionTaken(WebInspector.UserMetrics.Action.AnimationsPlaybackRateChanged);
        this._animationsPauseButton.element.classList.toggle("pause-toolbar-item");
        this._animationsPauseButton.element.classList.toggle("play-toolbar-item");
    },

    /**
     * @override
     * @return {!Promise<?>}
     */
    doUpdate: function()
    {
        if (!this._target)
            return Promise.resolve();

        /**
         * @param {number} playbackRate
         * @this {WebInspector.AnimationControlPane}
         */
        function setPlaybackRate(playbackRate)
        {
            this._animationsPlaybackSlider.value = WebInspector.AnimationTimeline.GlobalPlaybackRates.indexOf(playbackRate);
            this._animationsPlaybackLabel.textContent = playbackRate + "x";
        }

        return WebInspector.AnimationModel.fromTarget(this._target).playbackRatePromise().then(setPlaybackRate.bind(this));
    },

    /**
     * @override
     * @param {?WebInspector.DOMNode} node
     */
    onNodeChanged: function(node)
    {
        if (!node)
            return;

        if (this._target)
            this._target.resourceTreeModel.removeEventListener(WebInspector.ResourceTreeModel.EventTypes.MainFrameNavigated, this.update, this);

        this._target = node.target();
        this._target.resourceTreeModel.addEventListener(WebInspector.ResourceTreeModel.EventTypes.MainFrameNavigated, this.update, this);
        this.update();
    },

    __proto__: WebInspector.ElementsPanel.BaseToolbarPaneWidget.prototype
}

/**
 * @constructor
 * @implements {WebInspector.ToolbarItem.Provider}
 */
WebInspector.AnimationControlPane.ButtonProvider = function()
{
    this._button = new WebInspector.ToolbarButton(WebInspector.UIString("Toggle animation controls"), "animation-toolbar-item");
    this._button.addEventListener("click", this._clicked, this);
    WebInspector.context.addFlavorChangeListener(WebInspector.DOMNode, this._nodeChanged, this);
    this._nodeChanged();
}

WebInspector.AnimationControlPane.ButtonProvider.prototype = {
    /**
     * @param {boolean} toggleOn
     */
    _toggleAnimationControlPaneMode: function(toggleOn)
    {
        if (!this._animationsControlPane)
            this._animationsControlPane = new WebInspector.AnimationControlPane(this.item());
        WebInspector.ElementsPanel.instance().showToolbarPane(toggleOn ? this._animationsControlPane : null);
    },

    _clicked: function()
    {
        if (Runtime.experiments.isEnabled("animationInspection"))
            WebInspector.inspectorView.showViewInDrawer("animations");
        else
            this._toggleAnimationControlPaneMode(!this._button.toggled());
    },

    _nodeChanged: function()
    {
        var node = WebInspector.context.flavor(WebInspector.DOMNode);
        if (!Runtime.experiments.isEnabled("animationInspection")) {
            this._button.setEnabled(!!node);
            if (!node)
                this._toggleAnimationControlPaneMode(false);
        }
    },

    /**
     * @override
     * @return {!WebInspector.ToolbarItem}
     */
    item: function()
    {
        return this._button;
    }
}