; MaxCraft render-crash isolation: one file per A/B run.
;
; Each of these is the WHOLE [Debug] section that a run needs. Copy it over the [Debug] section of
; the MaxCraft.ini in the game folder (or copy the file straight over MaxCraft.ini and put the rest
; of your settings back) and launch the game.
;
; How to read a run: the log's second line is [BUILD] commit=..., and the line after
; "render: device hooks:" says how many of the eight hooks are actually installed. If that count is
; not what the file asked for, the run says nothing - check sDisabledHooks for a misspelt name, which
; is reported as "names no D3D8 device hook".
;
; The result is CRASH or NO CRASH. Do not record a guess: a run that neither reached Play nor wrote
; a CRASH line is "inconclusive", not "no crash".
;
; Known, measured before this set existed:
;   bNoRenderHooks = 1              -> NO CRASH   (the game runs)
;   bNoRenderHooks = 0,
;   bNoWvpFixup   = 1              -> CRASH     (so the WVP fixup is not the cause)

; --------------------------------------------------------------------------------------------
; test_00_baseline.ini - everything on. The run every other one is compared against.
; --------------------------------------------------------------------------------------------
[Debug]
bDiagnostics = 1
bNoRenderHooks = 0
bNoViewFixup = 0
bNoWvpFixup = 0
bNoBlockDraw = 0
bNoVtableSelfCheck = 0
sDisabledHooks =