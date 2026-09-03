# MDKParser integration

This directory vendors the parser from
[amanitan/MDKParser](https://github.com/amanitan/MDKParser) at commit
`a8f9a2f4fb3f0851020f53a6382d235d7b28d779` (2022-03-31).

The parser is kept in the `AetherKiri::MDKParser` namespace so its copied TJS2
lexer helpers do not collide with AetherKiri's core lexer.  `plugin.cpp` adapts
the upstream `MDKParser` native class to AetherKiri's internal plugin registry.
The Windows resource-backed message table is implemented by
`MDKMessages.cpp` so the same parser can be used on macOS and iOS.

The upstream license and notices are retained in `LICENSE.md`.
