; PREVIEW test: the file creates T5 itself (M563), so T5 need not be in the machine's tool table
M563 P5 S"3mm Drill"
;CNC_OP Profile
T1 M6
M3 S10000
G54
G0 Z5
G0 X0 Y0
G1 Z-1 F200
G1 X20 F600
G1 Y10
G0 Z5
;CNC_OP Drill
T5 M6
M3 S4000
G0 X10 Y5
G1 Z-3 F100
G0 Z5
M5
M2
