

This is WebCit-NG, a complete refactoring of the WebCit server that will
focus on "REST first" and build around that.  The code will be well
layered with as little spaghetti as possible.

Please don't mess with this yet.  I'm only pushing it upstream so it gets
backed up.


DESIGN GOALS:
-------------

*	Hold as little state as possible

*	Require NO cleanup.   Killing the process lets the OS reclaim all resources.

*	As much as possible, resources should be freed by just coming back down the stack.

*	Readability of the code is more important than shaving off a few CPU cycles.

*	Throw sensitive data such as passwords back and forth in clear text.
	If you want privacy, encrypt the whole session.  Anything else is false security.




REST format URIs will generally take the form of:

	/ctdl/objectClass/[container/]object[/operation]

