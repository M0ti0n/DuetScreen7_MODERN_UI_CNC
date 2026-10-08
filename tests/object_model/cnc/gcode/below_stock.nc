; PREVIEW test: the only warning is a cut below the stock bottom (information, asked once before RUN)
;CNC_STOCK X=30 Y=20 Z=2
;CNC_OP Profile
T1 M6
M3 S10000
G54
G0 Z5
G0 X5 Y5
G1 Z-3 F200
G1 X20 F600
G0 Z5
M5
M2
