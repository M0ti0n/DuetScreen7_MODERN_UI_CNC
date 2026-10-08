/*
 * CncGcode.cpp
 */

#include "CncGcode.h"
#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <fmt/format.h>
#include <map>
#include <set>

namespace UI::Cnc::Gcode
{
	void Box::add(float x, float y, float z)
	{
		if (empty)
		{
			minX = maxX = x;
			minY = maxY = y;
			minZ = maxZ = z;
			empty = false;
			return;
		}
		minX = std::min(minX, x);
		maxX = std::max(maxX, x);
		minY = std::min(minY, y);
		maxY = std::max(maxY, y);
		minZ = std::min(minZ, z);
		maxZ = std::max(maxZ, z);
	}

	void Box::add(const Box& other)
	{
		if (!other.empty)
		{
			add(other.minX, other.minY, other.minZ);
			add(other.maxX, other.maxY, other.maxZ);
		}
	}

	namespace
	{
		constexpr double Pi = 3.14159265358979323846;
		constexpr float ArcTolerance = 0.02f; // mm chord error of the drawn arcs
		constexpr size_t MaxArcSegments = 360;

		std::string upper(std::string_view s)
		{
			std::string out(s);
			for (char& c : out)
			{
				c = (char)std::toupper((unsigned char)c);
			}
			return out;
		}

		std::string trim(std::string_view s)
		{
			size_t a = 0, b = s.size();
			while (a < b && std::isspace((unsigned char)s[a]))
			{
				++a;
			}
			while (b > a && std::isspace((unsigned char)s[b - 1]))
			{
				--b;
			}
			return std::string(s.substr(a, b - a));
		}

		bool startsWithNoCase(std::string_view s, std::string_view prefix)
		{
			if (s.size() < prefix.size())
			{
				return false;
			}
			for (size_t i = 0; i < prefix.size(); ++i)
			{
				if (std::toupper((unsigned char)s[i]) != std::toupper((unsigned char)prefix[i]))
				{
					return false;
				}
			}
			return true;
		}

		bool parseFloat(std::string_view s, size_t& p, float& out)
		{
			size_t q = p;
			if (q < s.size() && (s[q] == '+'))
			{
				++q;
			}
			const char* begin = s.data() + q;
			const char* end = s.data() + s.size();
			auto r = std::from_chars(begin, end, out);
			if (r.ec != std::errc())
			{
				return false;
			}
			p = (size_t)(r.ptr - s.data());
			return true;
		}

		// "D=6", "D=6.35"
		bool keyValue(std::string_view text, std::string_view key, float& out)
		{
			const std::string u = upper(text);
			size_t at = 0;
			while ((at = u.find(key, at)) != std::string::npos)
			{
				const bool wordStart = at == 0 || !std::isalnum((unsigned char)u[at - 1]);
				size_t p = at + key.size();
				if (wordStart && parseFloat(u, p, out))
				{
					return true;
				}
				at += key.size();
			}
			return false;
		}

		// "6mm", "6 mm", "6.35MM"
		bool millimetres(std::string_view text, float& out)
		{
			const std::string u = upper(text);
			for (size_t i = 0; i < u.size(); ++i)
			{
				if (std::isdigit((unsigned char)u[i]) && (i == 0 || !std::isalnum((unsigned char)u[i - 1])))
				{
					size_t p = i;
					float v = 0;
					if (parseFloat(u, p, v))
					{
						size_t q = p;
						while (q < u.size() && u[q] == ' ')
						{
							++q;
						}
						if (u.compare(q, 2, "MM") == 0 && v > 0)
						{
							out = v;
							return true;
						}
						i = p;
					}
				}
			}
			return false;
		}

		// Tool type words -> short upper case type
		// The included tip angle of a pointed tool from its text: Fusion "TAPER=45deg" (half angle),
		// ";CNC_TOOL ... A=60", or "60°" / "60 deg" in the description. 0: not given.
		float tipAngle(std::string_view text)
		{
			float v = 0;
			if (keyValue(text, "TAPER=", v) && v > 0 && v < 180)
			{
				// Fusion: a drill's tip angle (118deg); a chamfer / engrave tool's taper from the axis (45deg)
				const std::string u = upper(text);
				return (u.find("DRILL") != std::string::npos || v >= 90) ? v : 2 * v;
			}
			if (keyValue(text, "A=", v) && v > 0 && v < 180)
			{
				return v;
			}
			const std::string u = upper(text);
			for (size_t i = 0; i < u.size(); ++i)
			{
				if (!std::isdigit((unsigned char)u[i]) ||
					(i > 0 && (std::isalnum((unsigned char)u[i - 1]) || u[i - 1] == '.')))
				{
					continue;
				}
				size_t p = i;
				float n = 0;
				if (!parseFloat(u, p, n))
				{
					continue;
				}
				size_t q = p;
				while (q < u.size() && u[q] == ' ')
				{
					++q;
				}
				const bool degree = u.compare(q, 2, "\xC2\xB0") == 0 || u.compare(q, 3, "DEG") == 0;
				if (degree && n > 0 && n < 180)
				{
					return n;
				}
				i = p;
			}
			return 0;
		}

		std::string toolTypeFrom(std::string_view text)
		{
			const std::string u = upper(text);
			struct Map
			{
				const char* word;
				const char* type;
			};
			static const Map Types[] = {{"FACE MILL", "FACE MILL"},
										{"FACEMILL", "FACE MILL"},
										{"BULL", "BULL"},
										{"BALL", "BALL"},
										{"SPOT", "SPOT DRILL"},
										{"CENTER DRILL", "SPOT DRILL"},
										{"DRILL", "DRILL"},
										{"CHAMFER", "CHAMFER"},
										{"ENGRAV", "ENGRAVE"},
										{"V-BIT", "V-BIT"},
										{"VBIT", "V-BIT"},
										{"THREAD", "THREAD"},
										{"TAP", "TAP"},
										{"REAM", "REAMER"},
										{"BORING", "BORING"},
										{"SLOT", "SLOT"},
										{"RADIUS", "RADIUS"},
										{"DOVETAIL", "DOVETAIL"},
										{"FLAT", "FLAT"},
										{"END MILL", "FLAT"},
										{"ENDMILL", "FLAT"}};
			for (const Map& m : Types)
			{
				if (u.find(m.word) != std::string::npos)
				{
					return m.type;
				}
			}
			return {};
		}

		struct ToolInfo
		{
			float diameter = 0;
			bool fileUnits = false; // D= in the file's units (inches after G20), not with "mm" in the text
			float angle = 0;		// included tip angle from the text (degrees), 0: not given
			std::string type;
			std::string text; // as written
		};

		struct Word
		{
			char letter;
			float value;
			bool hasValue;
		};

		struct State
		{
			bool known[3] = {false, false, false};
			double pos[3] = {0, 0, 0};
			bool absolute = true, arcAbsolute = false, inches = false;
			int plane = 17;
			int motion = 0; // 0..3, -1 none
			float feed = 0, spindle = 0;
			bool spindleOn = false, spindleCcw = false, coolant = false;
			unsigned int wcs = 1;
			int tool = -1;
		};

		class Parser
		{
		  public:
			Parser(const Options& o, Program& p)
				: m_opt(o)
				, m_prog(p)
			{
			}

			void run(std::string_view text)
			{
				prescan(text);
				size_t p = 0;
				while (p < text.size())
				{
					size_t e = text.find('\n', p);
					if (e == std::string_view::npos)
					{
						e = text.size();
					}
					std::string_view line = text.substr(p, e - p);
					if (!line.empty() && line.back() == '\r')
					{
						line.remove_suffix(1);
					}
					m_lineStart = (uint32_t)p;
					++m_prog.lines;
					line_(line);
					p = e + 1;
				}
				finish();
			}

		  private:
			// ---- Before reading: which comments repeat, does the file name its operations ----

			static bool nameCandidate(const std::string& c)
			{
				return !c.empty() && c.find('=') == std::string::npos && c.size() <= 40 &&
					   !startsWithNoCase(c, "Begin ") && !startsWithNoCase(c, "Finish ") &&
					   !startsWithNoCase(c, "Machine") && c.find(": ") == std::string::npos && !isToolDefinition(c) &&
					   !startsWithNoCase(c, "TC:") && !startsWithNoCase(c, "TC ") && !startsWithNoCase(c, "CNC_");
			}

			static bool cuttingWord(const std::string& codeUpper)
			{
				for (size_t i = 0; i + 1 < codeUpper.size(); ++i)
				{
					if (codeUpper[i] != 'G' || (i > 0 && std::isalpha((unsigned char)codeUpper[i - 1])))
					{
						continue;
					}
					size_t j = i + 1;
					while (j < codeUpper.size() && codeUpper[j] == '0')
					{
						++j;
					}
					if (j < codeUpper.size() && (codeUpper[j] == '1' || codeUpper[j] == '2' || codeUpper[j] == '3') &&
						(j + 1 == codeUpper.size() || !std::isdigit((unsigned char)codeUpper[j + 1])))
					{
						return true;
					}
				}
				return false;
			}

			void prescan(std::string_view text)
			{
				std::vector<std::string> afterCut; // name-like comment lines after the first cutting move
				bool cutSeen = false;
				size_t p = 0;
				while (p < text.size())
				{
					size_t e = text.find('\n', p);
					if (e == std::string_view::npos)
					{
						e = text.size();
					}
					const std::string line = trim(text.substr(p, e - p));
					p = e + 1;
					if (line.empty())
					{
						continue;
					}
					std::string c;
					if (line[0] == '(' && line.back() == ')')
					{
						c = trim(std::string_view(line).substr(1, line.size() - 2));
					}
					else if (line[0] == ';')
					{
						c = trim(std::string_view(line).substr(1));
					}
					else
					{
						if (!cutSeen)
						{
							std::string code = line.substr(0, std::min(line.find('('), line.find(';')));
							cutSeen = cuttingWord(upper(code));
						}
						continue;
					}
					if (startsWithNoCase(c, "Begin operation:") || startsWithNoCase(c, "CNC_OP"))
					{
						m_namedFile = true;
					}
					++m_commentCount[c];
					if (cutSeen && nameCandidate(c))
					{
						afterCut.push_back(c);
					}
				}
				for (const std::string& c : afterCut)
				{
					if (m_commentCount[c] < RepeatLimit)
					{
						m_namedFile = true; // a name between operations: names, not guesses, split them
					}
				}
			}

			// ---- Operations ----

			Operation& op()
			{
				if (m_prog.ops.empty())
				{
					newOp(m_lineStart);
				}
				return m_prog.ops.back();
			}

			// splitAt: the new operation starts at this point (moves already read after it move along)
			void newOp(uint32_t fileStart, uint32_t splitAt = UINT32_MAX)
			{
				const uint32_t first = (splitAt == UINT32_MAX) ? (uint32_t)m_prog.points.size() : splitAt;
				closeOp(first);
				Operation o;
				o.tool = m_st.tool;
				o.wcs = m_st.wcs;
				o.firstPoint = first;
				o.fileStart = fileStart;
				m_prog.ops.push_back(o);
				m_feedLength.clear();
				m_plungeLength.clear();
				m_cutSpindles.clear();
				m_opHasCut = false;
				m_opHasXY = false;
				m_opNamed = false;
				m_restart = false;
				m_atClearance = false;
			}

			void closeOp(uint32_t end = UINT32_MAX)
			{
				if (m_prog.ops.empty())
				{
					return;
				}
				Operation& o = m_prog.ops.back();
				o.pointCount = ((end == UINT32_MAX) ? (uint32_t)m_prog.points.size() : end) - o.firstPoint;
				if (m_opHasXY)
				{
					o.zMax = m_opTopXY;
				}
				double best = -1;
				for (auto& [f, len] : m_feedLength)
				{
					if (len > best)
					{
						best = len;
						o.feed = (float)f;
					}
				}
				best = -1;
				for (auto& [f, len] : m_plungeLength)
				{
					if (len > best)
					{
						best = len;
						o.plungeFeed = (float)f;
					}
				}
			}

			// No names in the file: does a cutting move start the next operation with the same tool?
			void maybeSplit()
			{
				if (m_opt.toolChangesOnly || m_namedFile || !m_opHasCut)
				{
					return;
				}
				const bool newFeed =
					m_st.feed > 0 && m_feedLength.find((int)std::lround(m_st.feed)) == m_feedLength.end();
				const bool newSpeed =
					m_st.spindle > 0 && m_cutSpindles.find((int)std::lround(m_st.spindle)) == m_cutSpindles.end();
				if (m_restart || (m_atClearance && (newFeed || newSpeed)))
				{
					newOp(m_atClearance ? m_clearanceLine : m_lineStart, m_atClearance ? m_clearancePoint : UINT32_MAX);
				}
				m_restart = false;
			}

			void nameOp(const std::string& name, uint32_t fileStart)
			{
				Operation& o = op();
				if (m_opHasCut)
				{
					if (m_opt.toolChangesOnly)
					{
						return; // operations only at tool changes: the first name stays
					}
					newOp(fileStart);
				}
				else if (m_prog.ops.back().pointCount == 0 && m_prog.points.size() == o.firstPoint)
				{
					m_prog.ops.back().fileStart = std::min(m_prog.ops.back().fileStart, fileStart);
				}
				m_prog.ops.back().name = name;
				m_opNamed = true;
			}

			// ---- Comments ----

			void toolComment(std::string_view c)
			{
				// "T1 D=6 CR=0 - ZMIN=-5 - flat end mill", "T3: 6mm Endmill", "CNC_TOOL T1 D=6 TYPE=FLAT"
				std::string_view s = c;
				if (startsWithNoCase(s, "CNC_TOOL"))
				{
					s.remove_prefix(8);
					while (!s.empty() && s[0] == ' ')
					{
						s.remove_prefix(1);
					}
				}
				size_t p = 1;
				float t = 0;
				if (!parseFloat(s, p, t) || t < 0)
				{
					return;
				}
				ToolInfo info;
				float d = 0;
				if (keyValue(s, "D=", d))
				{
					info.diameter = d;
					info.fileUnits = true;
				}
				else if (millimetres(s.substr(p), d))
				{
					info.diameter = d;
				}
				const std::string u = upper(s);
				const size_t typeAt = u.find("TYPE=");
				if (typeAt != std::string::npos)
				{
					size_t e = u.find(' ', typeAt);
					info.type = toolTypeFrom(u.substr(typeAt + 5, e == std::string::npos ? e : e - typeAt - 5));
					if (info.type.empty())
					{
						info.type = trim(u.substr(typeAt + 5, e == std::string::npos ? e : e - typeAt - 5));
					}
				}
				else
				{
					info.type = toolTypeFrom(s.substr(p));
				}
				info.angle = tipAngle(s);
				// The description: Fusion's after the last " - ", else what follows the number (and ':')
				{
					const std::string rest(s.substr(p));
					const size_t dash = rest.rfind(" - ");
					std::string text = (dash != std::string::npos) ? rest.substr(dash + 3) : rest;
					text = trim(text);
					if (!text.empty() && text[0] == ':')
					{
						text = trim(text.substr(1));
					}
					if (text.find('=') == std::string::npos && !text.empty())
					{
						info.text = text;
					}
				}
				ToolInfo& known = m_tools[(int)t];
				if (!info.text.empty())
				{
					known.text = info.text;
				}
				if (info.diameter > 0)
				{
					known.diameter = info.diameter;
					known.fileUnits = info.fileUnits;
				}
				if (!info.type.empty())
				{
					known.type = info.type;
				}
				if (info.angle > 0)
				{
					known.angle = info.angle;
				}
			}

			static bool isToolDefinition(std::string_view c)
			{
				if (startsWithNoCase(c, "CNC_TOOL"))
				{
					return true;
				}
				if (c.size() >= 2 && (c[0] == 'T' || c[0] == 't') && std::isdigit((unsigned char)c[1]))
				{
					size_t p = 1;
					while (p < c.size() && std::isdigit((unsigned char)c[p]))
					{
						++p;
					}
					return p == c.size() || c[p] == ' ' || c[p] == ':' || c[p] == '\t';
				}
				return false;
			}

			void stockComment(std::string_view c)
			{
				float x = 0, y = 0, z = 0, x0 = 0, y0 = 0, z0 = 0;
				if (!keyValue(c, "X=", x) || !keyValue(c, "Y=", y) || !keyValue(c, "Z=", z) || x <= 0 || y <= 0 ||
					z <= 0)
				{
					return;
				}
				keyValue(c, "X0=", x0);
				keyValue(c, "Y0=", y0);
				keyValue(c, "Z0=", z0);
				m_prog.stock = Box();
				m_prog.stock.add(x0, y0, z0 - z);
				m_prog.stock.add(x0 + x, y0 + y, z0);
				m_prog.stockFromFile = true;
			}

			// A comment; whole: nothing else on the line
			void comment(std::string_view raw, bool whole)
			{
				const std::string c = trim(raw);
				if (c.empty())
				{
					return;
				}
				if (startsWithNoCase(c, "CNC_STOCK"))
				{
					stockComment(c);
					return;
				}
				if (isToolDefinition(c))
				{
					toolComment(c);
					return;
				}
				if (startsWithNoCase(c, "TC:") || startsWithNoCase(c, "TC "))
				{
					m_pendingToolText = c.substr(3); // FreeCAD tool controller: for the next tool change
					return;
				}
				if (startsWithNoCase(c, "Begin operation:") || startsWithNoCase(c, "CNC_OP"))
				{
					m_markers = true;
					nameOp(trim(c.substr(startsWithNoCase(c, "CNC_OP") ? 6 : 16)), m_lineStart);
					m_pendingName.clear();
					return;
				}
				if (startsWithNoCase(c, "Finish operation:"))
				{
					m_markers = true;
					return;
				}
				if (m_markers || !whole)
				{
					return; // FreeCAD: only its markers name operations
				}
				// A name: short, not "key=value", not a header line, not repeated between moves
				if (!nameCandidate(c) || m_commentCount[c] >= RepeatLimit)
				{
					return;
				}
				m_pendingName = c;
				m_pendingOffset = m_lineStart;
			}

			// ---- Moves ----

			void storePoint(double x, double y, double z, PointKind kind)
			{
				const float fx = (float)x, fy = (float)y, fz = (float)z;
				std::vector<Point>& pts = m_prog.points;
				// Very large files: a move shorter than the tolerance is replaced by the next one of the same kind
				if (kind != PointKind::Start && m_mergeTol > 0 && pts.size() >= (size_t)op().firstPoint + 2)
				{
					Point& last = pts.back();
					const Point& prev = pts[pts.size() - 2];
					const float dx = last.x - prev.x, dy = last.y - prev.y, dz = last.z - prev.z;
					if (last.kind == kind && dx * dx + dy * dy + dz * dz < m_mergeTol * m_mergeTol)
					{
						last = {fx, fy, fz, kind};
						m_prog.pointOffsets.back() = m_lineStart; // now from this line
						return;
					}
				}
				pts.push_back({fx, fy, fz, kind});
				m_prog.pointOffsets.push_back(m_lineStart);
				if (pts.size() >= m_nextGrow)
				{
					m_mergeTol = (m_mergeTol == 0.0f) ? 0.05f : m_mergeTol * 2.0f;
					m_nextGrow += m_opt.maxPoints / 4;
					m_prog.truncated = true;
				}
			}

			void segment(const double from[3], const double to[3], bool rapid)
			{
				Operation& o = op();
				const double dx = to[0] - from[0], dy = to[1] - from[1], dz = to[2] - from[2];
				const double len = std::sqrt(dx * dx + dy * dy + dz * dz);
				o.travel.add((float)from[0], (float)from[1], (float)from[2]);
				o.travel.add((float)to[0], (float)to[1], (float)to[2]);
				if (rapid)
				{
					if (to[2] > from[2] && (float)to[2] >= o.travel.maxZ - 0.01f && m_opHasCut && !m_atClearance)
					{
						// up to the highest Z of the operation: between operations (or between regions of one)
						m_atClearance = true;
						m_clearancePoint = (uint32_t)m_prog.points.size(); // this move's end point comes next
						m_clearanceLine = m_lineStart;
					}
					o.rapidLength += len;
					o.seconds += len / (double)m_opt.rapidSpeed * 60.0;
				}
				else
				{
					o.cutLength += len;
					m_cutSpindles.insert((int)std::lround(m_st.spindle));
					if (std::hypot(dx, dy) > 1e-6)
					{
						m_atClearance = false; // cutting along XY again: the retract was inside the operation
					}
					const float f = (m_st.feed > 0) ? m_st.feed : 1000.0f;
					o.seconds += len / (double)f * 60.0;
					m_feedLength[(int)std::lround(m_st.feed)] += len;
					if (!m_opHasCut)
					{
						o.zMin = o.zMax = (float)from[2];
						m_opHasCut = true;
					}
					o.zMin = std::min({o.zMin, (float)from[2], (float)to[2]});
					o.zMax = std::max({o.zMax, (float)from[2], (float)to[2]});
					if (std::hypot(dx, dy) > 1e-6)
					{
						// the top of the cut: moves along XY, by their lower end (plunges and ramps start
						// above the stock)
						const float hi = std::min((float)from[2], (float)to[2]);
						m_opTopXY = m_opHasXY ? std::max(m_opTopXY, hi) : hi;
						m_opHasXY = true;
					}
					o.spindle = std::max(o.spindle, m_st.spindle);
					o.spindleCcw = o.spindleCcw || m_st.spindleCcw;
					o.coolant = o.coolant || m_st.coolant;
					if (o.cutLength == len)
					{
						o.wcs = m_st.wcs;
					}
					m_prog.cutBounds.add((float)from[0], (float)from[1], (float)from[2]);
					m_prog.cutBounds.add((float)to[0], (float)to[1], (float)to[2]);
					o.bounds.add((float)from[0], (float)from[1], (float)from[2]);
					o.bounds.add((float)to[0], (float)to[1], (float)to[2]);
					if (std::hypot(dx, dy) <= 1e-6 && dz < 0)
					{
						m_plungeLength[(int)std::lround(m_st.feed)] += len; // straight down
					}
					if (!m_st.spindleOn)
					{
						o.cutsWithSpindleOff = true;
					}
				}
				storePoint(to[0], to[1], to[2], rapid ? PointKind::Rapid : PointKind::Cut);
			}

			void linear(const double target[3], bool known[3], bool rapid)
			{
				const bool startKnown = m_st.known[0] && m_st.known[1] && m_st.known[2];
				for (int a = 0; a < 3; ++a)
				{
					if (known[a])
					{
						m_st.pos[a] = target[a];
						m_st.known[a] = true;
					}
				}
				const bool endKnown = m_st.known[0] && m_st.known[1] && m_st.known[2];
				if (!endKnown)
				{
					return;
				}
				if (!startKnown || m_needStart)
				{
					storePoint(m_st.pos[0], m_st.pos[1], m_st.pos[2], PointKind::Start);
					m_needStart = false;
					m_last[0] = m_st.pos[0];
					m_last[1] = m_st.pos[1];
					m_last[2] = m_st.pos[2];
					return;
				}
				segment(m_last, m_st.pos, rapid);
				m_last[0] = m_st.pos[0];
				m_last[1] = m_st.pos[1];
				m_last[2] = m_st.pos[2];
			}

			void arc(const double target[3],
					 bool known[3],
					 const double offs[3],
					 bool hasOffs[3],
					 float r,
					 bool hasR,
					 bool cw)
			{
				if (!(m_st.known[0] && m_st.known[1] && m_st.known[2]) || m_needStart)
				{
					linear(target, known, false);
					return;
				}
				// plane axes: a, b in the plane, c the helix axis
				int a = 0, b = 1, c = 2;
				if (m_st.plane == 18)
				{
					a = 2;
					b = 0;
					c = 1;
				}
				else if (m_st.plane == 19)
				{
					a = 1;
					b = 2;
					c = 0;
				}
				double end[3];
				for (int i = 0; i < 3; ++i)
				{
					end[i] = known[i] ? target[i] : m_st.pos[i];
				}
				const double sa = m_st.pos[a], sb = m_st.pos[b], ea = end[a], eb = end[b];
				double ca, cb;
				if (hasR && std::hypot(ea - sa, eb - sb) < 1e-9)
				{
					// R with the end at the start: no circle is defined (RRF refuses it): a straight move
					linear(target, known, false);
					return;
				}
				if (hasR)
				{
					const double dxa = ea - sa, dxb = eb - sb;
					const double d = std::sqrt(dxa * dxa + dxb * dxb);
					double rr = std::fabs(r);
					if (d < 1e-9 || rr < d / 2)
					{
						rr = std::max(rr, d / 2); // impossible arc: as wide as needed
					}
					const double h = std::sqrt(std::max(0.0, rr * rr - d * d / 4));
					double side = (cw ? -1 : 1) * (r < 0 ? -1 : 1);
					ca = sa + dxa / 2 - side * h * dxb / d;
					cb = sb + dxb / 2 + side * h * dxa / d;
				}
				else
				{
					const double oa = hasOffs[a] ? offs[a] : 0, ob = hasOffs[b] ? offs[b] : 0;
					ca = m_st.arcAbsolute ? oa : sa + oa;
					cb = m_st.arcAbsolute ? ob : sb + ob;
				}
				const double radius = std::hypot(sa - ca, sb - cb);
				double a0 = std::atan2(sb - cb, sa - ca), a1 = std::atan2(eb - cb, ea - ca);
				double sweep = a1 - a0;
				if (cw)
				{
					if (sweep >= -1e-9)
					{
						sweep -= 2 * Pi;
					}
				}
				else if (sweep <= 1e-9)
				{
					sweep += 2 * Pi;
				}
				if (hasR && std::fabs(sweep) > Pi + 1e-6 && r > 0)
				{
					sweep += cw ? 2 * Pi : -2 * Pi; // R > 0: the short way
				}
				const double step =
					(radius > (double)ArcTolerance) ? 2 * std::acos(1 - (double)ArcTolerance / radius) : Pi / 8;
				size_t n = (size_t)std::ceil(std::fabs(sweep) / std::max(step, 1e-3));
				n = std::clamp<size_t>(n, 1, MaxArcSegments);
				double prev[3] = {m_st.pos[0], m_st.pos[1], m_st.pos[2]};
				const double c0 = m_st.pos[c];
				for (size_t i = 1; i <= n; ++i)
				{
					const double t = (double)i / (double)n;
					double p[3];
					if (i == n)
					{
						p[0] = end[0];
						p[1] = end[1];
						p[2] = end[2];
					}
					else
					{
						const double ang = a0 + sweep * t;
						p[a] = ca + radius * std::cos(ang);
						p[b] = cb + radius * std::sin(ang);
						p[c] = c0 + (end[c] - c0) * t;
					}
					segment(prev, p, false);
					prev[0] = p[0];
					prev[1] = p[1];
					prev[2] = p[2];
				}
				for (int i = 0; i < 3; ++i)
				{
					m_st.pos[i] = end[i];
					m_last[i] = end[i];
				}
			}

			void toolChange(int t)
			{
				if (t == m_st.tool && !m_prog.ops.empty())
				{
					return;
				}
				m_st.tool = t;
				if (!m_pendingToolText.empty())
				{
					ToolInfo& info = m_tools[t];
					float d = 0;
					if (info.diameter == 0 && millimetres(m_pendingToolText, d))
					{
						info.diameter = d;
					}
					if (info.type.empty())
					{
						info.type = toolTypeFrom(m_pendingToolText);
					}
					if (info.angle == 0)
					{
						info.angle = tipAngle(m_pendingToolText);
					}
					m_pendingToolText.clear();
				}
				if (m_prog.ops.empty() || m_opHasCut)
				{
					newOp(m_pendingName.empty() ? m_lineStart : m_pendingOffset);
					if (!m_pendingName.empty() && !m_markers)
					{
						m_prog.ops.back().name = m_pendingName;
						m_opNamed = true;
						m_pendingName.clear();
					}
				}
				m_prog.ops.back().tool = t;
			}

			// ---- A line ----

			void line_(std::string_view line)
			{
				// Comments out, words in
				std::string code;
				code.reserve(line.size());
				bool hadComment = false;
				std::string commentText;
				for (size_t i = 0; i < line.size(); ++i)
				{
					const char ch = line[i];
					if (ch == '(')
					{
						const size_t e = line.find(')', i);
						commentText =
							std::string(line.substr(i + 1, (e == std::string_view::npos ? line.size() : e) - i - 1));
						hadComment = true;
						const bool whole = trim(code).empty() &&
										   trim(line.substr(e == std::string_view::npos ? line.size() : e + 1)).empty();
						comment(commentText, whole);
						if (e == std::string_view::npos)
						{
							break;
						}
						i = e;
						continue;
					}
					if (ch == ';')
					{
						commentText = std::string(line.substr(i + 1));
						hadComment = true;
						comment(commentText, trim(code).empty());
						break;
					}
					code += ch;
				}
				(void)hadComment;
				const std::string t = trim(code);
				if (t.empty() || t[0] == '%')
				{
					return;
				}
				// RRF meta commands and expressions
				static const char* const Meta[] = {"if",
												   "elif",
												   "else",
												   "while",
												   "break",
												   "continue",
												   "var",
												   "global",
												   "set",
												   "echo",
												   "abort",
												   "return"};
				for (const char* m : Meta)
				{
					const size_t n = std::char_traits<char>::length(m);
					if (t.compare(0, n, m) == 0 && (t.size() == n || !std::isalnum((unsigned char)t[n])))
					{
						return;
					}
				}

				std::vector<Word> words;
				for (size_t i = 0; i < t.size();)
				{
					const char ch = (char)std::toupper((unsigned char)t[i]);
					if (std::isalpha((unsigned char)ch))
					{
						size_t p = i + 1;
						while (p < t.size() && t[p] == ' ')
						{
							++p;
						}
						Word w{ch, 0, false};
						if (p < t.size() && t[p] == '{')
						{
							const size_t e = t.find('}', p);
							i = (e == std::string::npos) ? t.size() : e + 1; // expression: unknown value
							words.push_back(w);
							continue;
						}
						if (p < t.size() && t[p] == '"')
						{
							const size_t e = t.find('"', p + 1);
							i = (e == std::string::npos) ? t.size() : e + 1;
							words.push_back(w);
							continue;
						}
						float v = 0;
						if (parseFloat(t, p, v))
						{
							w.value = v;
							w.hasValue = true;
							i = p;
						}
						else
						{
							i = i + 1;
						}
						words.push_back(w);
					}
					else if (ch == '*')
					{
						break; // checksum
					}
					else
					{
						++i;
					}
				}
				execute(words);
			}

			void execute(const std::vector<Word>& words)
			{
				double target[3] = {0, 0, 0}, offs[3] = {0, 0, 0};
				bool hasAxis[3] = {false, false, false}, hasOffs[3] = {false, false, false};
				float r = 0;
				bool hasR = false, g53 = false, motionWord = false;
				int toolWord = -2;
				const double unit = m_st.inches ? 25.4 : 1.0;

				// S and F of other commands (G4 S3 dwell, M42 P0 S1, M106 S255, M568 F...) are not the spindle
				// speed or the feed
				bool otherM = false, dwell = false;
				for (const Word& w : words)
				{
					if (!w.hasValue)
					{
						continue;
					}
					if (w.letter == 'M')
					{
						const int m = (int)std::lround(w.value);
						otherM = otherM || m < 3 || m > 9; // M3..M9: spindle, tool change, coolant
					}
					else if (w.letter == 'G' && std::lround(w.value * 10) == 40)
					{
						dwell = true;
					}
				}

				// Modes first (G20 / G21 / G90 / G91 apply to the axis words of the same line)
				for (const Word& w : words)
				{
					if (w.letter != 'G' || !w.hasValue)
					{
						continue;
					}
					const int g10 = (int)std::lround(w.value * 10);
					switch (g10)
					{
					case 200:
						m_st.inches = true;
						m_inchFile = true;
						break;
					case 210:
						m_st.inches = false;
						break;
					case 900:
						m_st.absolute = true;
						break;
					case 910:
						m_st.absolute = false;
						break;
					case 901:
						m_st.arcAbsolute = true;
						break;
					case 911:
						m_st.arcAbsolute = false;
						break;
					case 170:
					case 180:
					case 190:
						m_st.plane = g10 / 10;
						break;
					case 530:
						g53 = true;
						break;
					case 0:
					case 10:
					case 20:
					case 30:
						m_st.motion = g10 / 10;
						motionWord = true;
						break;
					case 800:
						m_st.motion = -1;
						break;
					default:
						if (g10 >= 540 && g10 <= 590)
						{
							m_st.wcs = (unsigned int)(g10 / 10 - 53);
							m_restart = m_restart || m_opHasCut; // the work offset given again
						}
						else if (g10 == 591 || g10 == 592 || g10 == 593)
						{
							m_st.wcs = (unsigned int)(6 + g10 - 590);
						}
						break;
					}
				}
				const double u = m_st.inches ? 25.4 : unit;
				bool otherG = false;
				for (const Word& w : words)
				{
					if (!w.hasValue)
					{
						continue;
					}
					switch (w.letter)
					{
					case 'X':
					case 'Y':
					case 'Z':
					{
						const int a = w.letter - 'X';
						target[a] = (double)w.value * u;
						hasAxis[a] = true;
						break;
					}
					case 'I':
					case 'J':
					case 'K':
					{
						const int a = w.letter - 'I';
						offs[a] = (double)w.value * u;
						hasOffs[a] = true;
						break;
					}
					case 'R':
						r = (float)((double)w.value * u);
						hasR = true;
						break;
					case 'F':
						if (!otherM)
						{
							m_st.feed = (float)((double)w.value * u);
						}
						break;
					case 'S':
						if (!otherM && !dwell)
						{
							m_st.spindle = w.value;
						}
						break;
					case 'T':
						toolWord = (int)std::lround(w.value);
						break;
					case 'M':
					{
						const int m = (int)std::lround(w.value);
						if (m == 3 || m == 4)
						{
							m_restart = m_restart || m_opHasCut; // the spindle started again: a new operation?
							m_st.spindleOn = true;
							m_st.spindleCcw = m == 4;
						}
						else if (m == 5)
						{
							m_st.spindleOn = false;
						}
						else if (m == 7 || m == 8)
						{
							m_st.coolant = true;
						}
						else if (m == 9)
						{
							m_st.coolant = false;
						}
						else if (m == 563)
						{
							// The file creates a tool (M563 P<n>): it need not be in the machine's tool table
							for (const Word& p : words)
							{
								if (p.letter == 'P' && p.hasValue)
								{
									const int t = (int)std::lround(p.value);
									auto& d = m_prog.definedTools;
									const auto at = std::lower_bound(d.begin(), d.end(), t);
									if (at == d.end() || *at != t)
									{
										d.insert(at, t);
									}
								}
							}
						}
						break;
					}
					case 'G':
					{
						const int g10 = (int)std::lround(w.value * 10);
						if (g10 == 40 || g10 == 100 || g10 == 280 || g10 == 300 || g10 == 920 || g10 == 380 ||
							g10 == 381 || g10 == 382 || g10 == 383 || g10 == 384 || g10 == 385)
						{
							otherG = true; // dwell, G10, homing, G92, probing: no drawn move
						}
						break;
					}
					default:
						break;
					}
				}
				if (toolWord >= -1)
				{
					toolChange(toolWord);
				}
				const bool anyAxis = hasAxis[0] || hasAxis[1] || hasAxis[2];
				if (!anyAxis)
				{
					return;
				}
				if (g53 || otherG)
				{
					// Machine coordinates, homing, probing, G92: the work position of these axes is unknown after
					for (int a = 0; a < 3; ++a)
					{
						if (hasAxis[a])
						{
							m_st.known[a] = false;
						}
					}
					m_needStart = true;
					return;
				}
				if (m_st.motion < 0 && !motionWord)
				{
					return;
				}
				// A named operation starts at its first move (Fusion: the comment line before it)
				if (!m_pendingName.empty() && !m_markers)
				{
					if (m_opHasCut || m_prog.ops.empty() || !m_opNamed)
					{
						nameOp(m_pendingName, m_pendingOffset);
					}
					m_pendingName.clear();
				}
				if (!m_st.absolute)
				{
					for (int a = 0; a < 3; ++a)
					{
						if (hasAxis[a])
						{
							target[a] += m_st.pos[a];
						}
					}
				}
				if (m_st.motion >= 1)
				{
					maybeSplit();
				}
				if (m_st.motion <= 1)
				{
					linear(target, hasAxis, m_st.motion == 0);
				}
				else
				{
					arc(target, hasAxis, offs, hasOffs, r, hasR, m_st.motion == 2);
				}
			}

			// ---- End ----

			void finish()
			{
				closeOp();
				std::vector<Operation>& ops = m_prog.ops;
				// Operations without cutting moves join the one before (or the next one when first)
				std::vector<Operation> kept;
				for (Operation& o : ops)
				{
					if (o.cutLength > 0)
					{
						if (!kept.empty() && kept.back().cutLength == 0)
						{
							// a leading op without cuts: its moves go into this one
							Operation lead = kept.back();
							kept.pop_back();
							o.firstPoint = lead.firstPoint;
							o.travel.add(lead.travel);
							o.pointCount += lead.pointCount;
							o.rapidLength += lead.rapidLength;
							o.seconds += lead.seconds;
							o.fileStart = std::min(o.fileStart, lead.fileStart);
							if (o.name.empty())
							{
								o.name = lead.name;
							}
						}
						kept.push_back(o);
					}
					else if (!kept.empty() && kept.back().cutLength > 0)
					{
						Operation& prev = kept.back();
						prev.pointCount += o.pointCount;
						prev.travel.add(o.travel);
						prev.rapidLength += o.rapidLength;
						prev.seconds += o.seconds;
					}
					else if (!kept.empty())
					{
						kept.back().pointCount += o.pointCount;
						kept.back().travel.add(o.travel);
						kept.back().rapidLength += o.rapidLength;
						kept.back().seconds += o.seconds;
					}
					else
					{
						kept.push_back(o);
					}
				}
				if (!kept.empty() && kept.back().cutLength == 0 && kept.size() > 1)
				{
					Operation last = kept.back();
					kept.pop_back();
					kept.back().pointCount += last.pointCount;
					kept.back().travel.add(last.travel);
					kept.back().rapidLength += last.rapidLength;
					kept.back().seconds += last.seconds;
				}
				ops = std::move(kept);

				std::vector<int> tools;
				m_prog.seconds = 0;
				float maxRadius = 0;
				// Unnamed: "T2", or "T2 (1)", "T2 (2)" when the tool has more than one
				std::map<int, int> unnamedPerTool, unnamedSeen;
				for (const Operation& o : ops)
				{
					if (o.name.empty())
					{
						++unnamedPerTool[o.tool];
					}
				}
				for (Operation& o : ops)
				{
					if (o.name.empty())
					{
						const std::string base = (o.tool >= 0) ? fmt::format("T{:d}", o.tool) : std::string("OP");
						o.name = (unnamedPerTool[o.tool] > 1) ? fmt::format("{:s} ({:d})", base, ++unnamedSeen[o.tool])
															  : base;
					}
					auto it = m_tools.find(o.tool);
					if (it != m_tools.end())
					{
						// Fusion's "(T1 D=0.25 ...)" in an inch program: inches, like the moves
						const float scale = (it->second.fileUnits && m_inchFile) ? 25.4f : 1.0f;
						o.toolDiameter = it->second.diameter * scale;
						o.toolType = it->second.type;
						// Pointed tools: the angle given, else the usual one; others are not pointed
						const std::string& ty = o.toolType;
						const bool vee = ty == "V-BIT" || ty == "CHAMFER" || ty == "ENGRAVE" || ty == "SPOT DRILL";
						const bool drill = ty == "DRILL";
						o.toolAngle = (vee || drill || ty.empty()) ? it->second.angle : 0.0f;
						if (o.toolAngle == 0 && (vee || drill))
						{
							o.toolAngle = drill ? 118.0f : 90.0f;
						}
						o.toolText = it->second.text;
						maxRadius = std::max(maxRadius, o.toolDiameter / 2);
					}
					if (o.tool >= 0 && std::find(tools.begin(), tools.end(), o.tool) == tools.end())
					{
						tools.push_back(o.tool);
					}
					m_prog.seconds += o.seconds;
				}
				m_prog.toolCount = tools.size();

				if (!m_prog.stockFromFile && !m_prog.cutBounds.empty)
				{
					// Around the cutting moves, by the largest tool radius; top at Z0 when the job cuts below it (work
					// zero on the stock top, the usual case), else the highest cutting move
					const Box& c = m_prog.cutBounds;
					const float top = (c.minZ < 0.0f) ? 0.0f : c.maxZ;
					float bottom = c.minZ;
					if (top - bottom < 0.5f)
					{
						bottom = top - 0.5f;
					}
					m_prog.stock = Box();
					m_prog.stock.add(c.minX - maxRadius, c.minY - maxRadius, bottom);
					m_prog.stock.add(c.maxX + maxRadius, c.maxY + maxRadius, top);
				}
			}

			const Options& m_opt;
			Program& m_prog;
			State m_st;
			double m_last[3] = {0, 0, 0};
			bool m_needStart = false;
			bool m_opHasCut = false, m_opNamed = false, m_opHasXY = false;
			float m_opTopXY = 0;
			bool m_markers = false;
			std::string m_pendingName, m_pendingToolText;
			uint32_t m_pendingOffset = 0, m_lineStart = 0;
			std::map<int, ToolInfo> m_tools;
			bool m_inchFile = false; // G20 in the file
			std::map<int, double> m_feedLength, m_plungeLength;
			std::set<int> m_cutSpindles;			   // spindle speeds the operation has cut with
			std::map<std::string, int> m_commentCount; // whole-line comments, how often
			bool m_namedFile = false;				   // the file names its operations
			bool m_restart = false;					   // spindle started / work offset given after cutting
			bool m_atClearance = false;				   // retracted to the operation's highest Z, not cut since
			uint32_t m_clearancePoint = 0, m_clearanceLine = 0;
			static constexpr int RepeatLimit = 3;
			float m_mergeTol = 0;
			size_t m_nextGrow = 0;

		  public:
			void init() { m_nextGrow = m_opt.maxPoints; }
		};
	} // namespace

	bool Program::definesTool(int tool) const
	{
		return std::binary_search(definedTools.begin(), definedTools.end(), tool);
	}

	Program Parse(std::string_view text, const Options& options)
	{
		Program prog;
		Parser p(options, prog);
		p.init();
		p.run(text);
		return prog;
	}

	std::array<std::string, 2> ShortName(std::string_view name)
	{
		struct Abbr
		{
			const char* word;
			const char* shortWord;
		};
		static const Abbr Abbrs[] = {
			{"ADAPTIVE", "ADAPT"},	  {"CLEARING", "CLEAR"},   {"ROUGHING", "ROUGH"},	  {"FINISHING", "FINISH"},
			{"DRILLING", "DRILL"},	  {"FACING", "FACE"},	   {"PARALLEL", "PARALL"},	  {"HORIZONTAL", "HORIZ"},
			{"ENGRAVING", "ENGRAVE"}, {"BORING", "BORE"},	   {"THREADING", "THREAD"},	  {"CHAMFERING", "CHAMFER"},
			{"PROFILING", "PROFILE"}, {"POCKETING", "POCKET"}, {"CONTOURING", "CONTOUR"}, {"SCALLOPING", "SCALLOP"},
			{"SURFACING", "SURFACE"}, {"SLOTTING", "SLOT"},	   {"SPIRALLING", "SPIRAL"},  {"OPERATION", "OP"},
			{"DEBURRING", "DEBURR"},  {"HELICAL", "HELIX"},	   {"MORPHED", "MORPH"},	  {"PROJECTED", "PROJECT"}};
		// "T2 (1)": the tool on the first row, the piece on the second
		{
			auto digits = [](std::string_view v)
			{
				return !v.empty() &&
					   std::all_of(v.begin(), v.end(), [](char c) { return std::isdigit((unsigned char)c) != 0; });
			};
			const size_t open = name.find(" (");
			if (open != std::string_view::npos && name.back() == ')')
			{
				const std::string_view head = name.substr(0, open),
									   piece = name.substr(open + 2, name.size() - open - 3);
				if (((head.size() >= 2 && head[0] == 'T' && digits(head.substr(1))) || head == "OP") && digits(piece))
				{
					return {std::string(head), "(" + std::string(piece) + ")"};
				}
			}
		}
		std::vector<std::string> words;
		std::string cur;
		auto flush = [&]
		{
			// trailing digits off ("Contour2" -> CONTOUR) when 2 letters or more remain
			size_t e = cur.size();
			while (e > 0 && std::isdigit((unsigned char)cur[e - 1]))
			{
				--e;
			}
			if (e >= 2)
			{
				cur.resize(e); // "T3" stays: the tool is all an unnamed operation has
			}
			if (!cur.empty())
			{
				words.push_back(cur);
			}
			cur.clear();
		};
		for (char ch : name)
		{
			if (std::isalnum((unsigned char)ch))
			{
				cur += (char)std::toupper((unsigned char)ch);
			}
			else
			{
				flush();
			}
		}
		flush();
		if (words.size() > 2 && (words[0] == "2D" || words[0] == "3D"))
		{
			words.erase(words.begin());
		}
		std::array<std::string, 2> out;
		for (size_t i = 0; i < 2 && i < words.size(); ++i)
		{
			std::string w = words[i];
			for (const Abbr& a : Abbrs)
			{
				if (w == a.word)
				{
					w = a.shortWord;
					break;
				}
			}
			if (w.size() > MaxShortLetters)
			{
				w.resize(MaxShortLetters);
			}
			out[i] = w;
		}
		if (out[0].empty())
		{
			out[0] = "OP";
		}
		return out;
	}

	std::string FormatTime(double seconds)
	{
		long s = (long)std::lround(std::max(0.0, seconds));
		if (s >= 3600)
		{
			return fmt::format("{:d}:{:02d}:{:02d}", s / 3600, (s / 60) % 60, s % 60);
		}
		return fmt::format("{:d}:{:02d}", s / 60, s % 60);
	}

	uint32_t PointsBefore(const Program& program, uint32_t filePosition)
	{
		const auto& o = program.pointOffsets;
		return (uint32_t)(std::lower_bound(o.begin(), o.end(), filePosition) - o.begin());
	}

	int OperationAt(const Program& program, uint32_t filePosition)
	{
		int found = -1;
		for (size_t i = 0; i < program.ops.size(); ++i)
		{
			if (program.ops[i].fileStart <= filePosition)
			{
				found = (int)i;
			}
		}
		return found;
	}
} // namespace UI::Cnc::Gcode
