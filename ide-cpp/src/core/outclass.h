#pragma once

// what a piece of console output is, rather than what colour it is today: the
// theme can change under it, and then every chunk is coloured again.
enum OutClass { OC_TEXT = 0, OC_ERROR, OC_SUCCESS, OC_INFO, OC_USERIN, OC_PROMPT };
