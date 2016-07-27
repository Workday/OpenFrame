description("Test the computed style of the -webkit-filter property.");

// These have to be global for the test helpers to see them.
var filterStyle;
var styleElement = document.createElement("style");
document.head.appendChild(styleElement);
var stylesheet = styleElement.sheet;

function testComputedFilterRule(description, rule, expectedValue)
{
    if (expectedValue === undefined)
        expectedValue = rule;

    debug("");
    debug(description + " : " + rule);

    stylesheet.insertRule("body { -webkit-filter: " + rule + "; }", 0);

    filterStyle = window.getComputedStyle(document.body).getPropertyValue('-webkit-filter');
    shouldBeEqualToString("filterStyle", expectedValue);
    stylesheet.deleteRule(0);
}

testComputedFilterRule("Basic reference", "url('#a')");
testComputedFilterRule("Bare unquoted reference converting to quoted form", "url(#a)", "url('#a')");
testComputedFilterRule("Multiple references", "url('#a') url('#b')");
testComputedFilterRule("Reference as 2nd value", "grayscale(1) url('#a')");
testComputedFilterRule("Integer value", "grayscale(1)");
testComputedFilterRule("Float value converts to integer", "grayscale(1.0)", "grayscale(1)");
testComputedFilterRule("Zero value", "grayscale(0)");
testComputedFilterRule("No values", "grayscale()", "grayscale(1)");
testComputedFilterRule("Multiple values", "grayscale(0.5) grayscale(0.25)");
testComputedFilterRule("Integer value", "sepia(1)");
testComputedFilterRule("Float value converts to integer", "sepia(1.0)", "sepia(1)");
testComputedFilterRule("Zero value", "sepia(0)");
testComputedFilterRule("No values", "sepia()", "sepia(1)");
testComputedFilterRule("Multiple values", "sepia(0.5) sepia(0.25)");
testComputedFilterRule("Rule combinations", "sepia(0.5) grayscale(0.25)");
testComputedFilterRule("Integer value", "saturate(1)");
testComputedFilterRule("Float value converts to integer", "saturate(1.0)", "saturate(1)");
testComputedFilterRule("Zero value", "saturate(0)");
testComputedFilterRule("No values", "saturate()", "saturate(1)");
testComputedFilterRule("Multiple values", "saturate(0.5) saturate(0.25)");
testComputedFilterRule("Rule combinations", "saturate(0.5) grayscale(0.25)");
testComputedFilterRule("Degrees value as integer", "hue-rotate(10deg)");
testComputedFilterRule("Degrees float value converts to integer", "hue-rotate(10.0deg)", "hue-rotate(10deg)");
testComputedFilterRule("Radians value", "hue-rotate(10rad)", "hue-rotate(572.958deg)");
testComputedFilterRule("Gradians value", "hue-rotate(10grad)", "hue-rotate(9deg)");
testComputedFilterRule("Turns value", "hue-rotate(0.5turn)", "hue-rotate(180deg)");
testComputedFilterRule("Zero value", "hue-rotate(0)", "hue-rotate(0deg)");
testComputedFilterRule("No values", "hue-rotate()", "hue-rotate(0deg)");
testComputedFilterRule("Rule combinations", "hue-rotate(10deg) grayscale(0.25)");
testComputedFilterRule("Integer value", "invert(1)");
testComputedFilterRule("Float value converts to integer", "invert(1.0)", "invert(1)");
testComputedFilterRule("Zero value", "invert(0)");
testComputedFilterRule("No values", "invert()", "invert(1)");
testComputedFilterRule("Multiple values", "invert(0.5) invert(0.25)");
testComputedFilterRule("Rule combinations", "invert(0.5) grayscale(0.25)");
testComputedFilterRule("Integer value", "opacity(1)");
testComputedFilterRule("Float value converts to integer", "opacity(1.0)", "opacity(1)");
testComputedFilterRule("Zero value", "opacity(0)");
testComputedFilterRule("No values", "opacity()", "opacity(1)");
testComputedFilterRule("Multiple values", "opacity(0.5) opacity(0.25)");
testComputedFilterRule("Rule combinations", "opacity(0.5) grayscale(0.25)");
testComputedFilterRule("Integer value", "brightness(1)");
testComputedFilterRule("Float value converts to integer", "brightness(1.0)", "brightness(1)");
testComputedFilterRule("Zero value", "brightness(0)");
testComputedFilterRule("No values", "brightness()", "brightness(0)");
testComputedFilterRule("Multiple values", "brightness(0.5) brightness(0.25)");
testComputedFilterRule("Rule combinations", "brightness(0.5) grayscale(0.25)");
testComputedFilterRule("Integer value", "contrast(1)");
testComputedFilterRule("Value greater than 1", "contrast(2)");
testComputedFilterRule("Float value converts to integer", "contrast(1.0)", "contrast(1)");
testComputedFilterRule("Zero value", "contrast(0)");
testComputedFilterRule("No values", "contrast()", "contrast(1)");
testComputedFilterRule("Multiple values", "contrast(0.5) contrast(0.25)");
testComputedFilterRule("Rule combinations", "contrast(0.5) grayscale(0.25)");
testComputedFilterRule("One zero to px", "blur(0)", "blur(0px)");
testComputedFilterRule("One length", "blur(2em)", "blur(32px)");
testComputedFilterRule("One length", "blur(5px)");
testComputedFilterRule("No values", "blur()", "blur(0px)");

testComputedFilterRule("Color then three values",
    "drop-shadow(red 1px 2px 3px)", "drop-shadow(rgb(255, 0, 0) 1px 2px 3px)");

testComputedFilterRule("Three values then color",
    "drop-shadow(1px 2px 3px red)", "drop-shadow(rgb(255, 0, 0) 1px 2px 3px)");

testComputedFilterRule("Color then three values with zero length",
    "drop-shadow(#abc 0 0 0)", "drop-shadow(rgb(170, 187, 204) 0px 0px 0px)");

testComputedFilterRule("Three values with zero length",
    "drop-shadow(0 0 0)", "drop-shadow(rgb(0, 0, 0) 0px 0px 0px)");

testComputedFilterRule("Two values no color",
    "drop-shadow(1px 2px)", "drop-shadow(rgb(0, 0, 0) 1px 2px 0px)");

testComputedFilterRule("Multiple operations",
    "grayscale(0.5) sepia(0.25) saturate(0.75) hue-rotate(35deg) invert(0.2) opacity(0.9) blur(5px)");

testComputedFilterRule("Percentage values",
    "grayscale(50%) sepia(25%) saturate(75%) invert(20%) opacity(90%) brightness(60%) contrast(30%)",
    "grayscale(0.5) sepia(0.25) saturate(0.75) invert(0.2) opacity(0.9) brightness(0.6) contrast(0.3)");

successfullyParsed = true;
