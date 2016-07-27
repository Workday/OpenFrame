description(
    "This test checks an orphan text node cannot be surrounded by the range. (bug31684)"
);

var range = document.createRange();
var text = document.createTextNode('hello');
var element = document.createElement("div");
range.selectNodeContents(text);

shouldThrow("range.surroundContents(element)", '"HierarchyRequestError: Failed to execute \'surroundContents\' on \'Range\': The container node is a detached character data node; no parent node is available for insertion."');
