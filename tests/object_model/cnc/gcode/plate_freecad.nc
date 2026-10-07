(Exported by FreeCAD)
(Post Processor: grbl_post)
(Output Time:2026-10-07)
(Begin preamble)
G17 G90
G21
(Begin operation: Fixture)
(Machine units: mm/min)
G54
(Finish operation: Fixture)
(Begin operation: TC: 6mm Endmill)
(Machine units: mm/min)
(TC: 6mm Endmill)
M6 T2
M3 S10000
(Finish operation: TC: 6mm Endmill)
(Begin operation: Profile)
(Machine units: mm/min)
G0 Z10
G0 X0 Y0
G0 Z1
G1 Z-3 F200
G1 X50 F600
G1 Y30
G1 X0
G1 Y0
G0 Z10
(Finish operation: Profile)
(Begin operation: Pocket)
G0 X10 Y10
G0 Z1
G1 Z-2 F200
G91
G1 X30 F600
G1 Y10
G1 X-30
G1 Y-10
G90
G0 Z10
(Finish operation: Pocket)
(Begin operation: TC: 3mm Drill)
(TC: 3mm Drill)
M6 T5
M3 S4000
(Finish operation: TC: 3mm Drill)
(Begin operation: Drilling)
G0 X25 Y15
G0 Z3
G1 Z-10 F100
G0 Z10
(Finish operation: Drilling)
(Begin postamble)
M5
G17 G90
M2
