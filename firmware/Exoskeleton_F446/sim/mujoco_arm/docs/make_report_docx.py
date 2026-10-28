#!/usr/bin/env python3
"""Build the bilingual (Chinese/English) validation report as a .docx.

The numbers are the ones recorded in docs/validation_report.md and in
build/sim-runs/report-muscle-final2/. Regenerate the document after re-running
the experiments:

    build\\sim-venv\\Scripts\\python.exe sim\\mujoco_arm\\docs\\make_report_docx.py
"""

from __future__ import annotations

from pathlib import Path

from docx import Document
from docx.enum.table import WD_TABLE_ALIGNMENT
from docx.enum.text import WD_ALIGN_PARAGRAPH
from docx.oxml import OxmlElement
from docx.oxml.ns import qn
from docx.shared import Cm, Pt, RGBColor

DOCS = Path(__file__).resolve().parent
FIGURES = DOCS / "figures"
OUT = DOCS / "validation_report_bilingual.docx"   # override with --out

ZH_FONT = "Microsoft YaHei"
EN_FONT = "Calibri"
ACCENT = RGBColor(0x1F, 0x3B, 0x57)
GREY = RGBColor(0x55, 0x55, 0x55)


# --------------------------------------------------------------------------
# helpers
# --------------------------------------------------------------------------

def set_run_font(run, size=10.5, bold=False, italic=False, colour=None, east_asian=ZH_FONT):
    run.font.name = EN_FONT
    run.font.size = Pt(size)
    run.bold = bold
    run.italic = italic
    if colour is not None:
        run.font.color.rgb = colour
    rpr = run._element.get_or_add_rPr()
    rfonts = rpr.find(qn("w:rFonts"))
    if rfonts is None:
        rfonts = OxmlElement("w:rFonts")
        rpr.append(rfonts)
    rfonts.set(qn("w:eastAsia"), east_asian)


def para(doc, text, size=10.5, bold=False, italic=False, colour=None,
         space_after=4, align=None):
    p = doc.add_paragraph()
    p.paragraph_format.space_after = Pt(space_after)
    p.paragraph_format.line_spacing = 1.15
    if align is not None:
        p.alignment = align
    run = p.add_run(text)
    set_run_font(run, size=size, bold=bold, italic=italic, colour=colour)
    return p


def bi(doc, zh, en, size=10.5, space_after=8):
    """Chinese paragraph followed by the English one in a lighter style."""
    para(doc, zh, size=size, space_after=2)
    para(doc, en, size=size - 0.5, italic=True, colour=GREY, space_after=space_after)


def heading(doc, zh, en, level=1):
    p = doc.add_paragraph()
    p.paragraph_format.space_before = Pt(14 if level == 1 else 10)
    p.paragraph_format.space_after = Pt(4)
    run = p.add_run(zh)
    set_run_font(run, size=15 if level == 1 else 12.5, bold=True, colour=ACCENT)
    run2 = p.add_run("  /  " + en)
    set_run_font(run2, size=15 if level == 1 else 12.5, bold=False, italic=True, colour=GREY)
    return p


def bullet(doc, zh, en):
    para(doc, "• " + zh, size=10.5, space_after=1)
    para(doc, "   " + en, size=9.5, italic=True, colour=GREY, space_after=6)


def table(doc, header, rows, widths=None, size=9):
    t = doc.add_table(rows=1, cols=len(header))
    t.style = "Table Grid"
    t.alignment = WD_TABLE_ALIGNMENT.CENTER
    for cell, text in zip(t.rows[0].cells, header):
        cell.text = ""
        run = cell.paragraphs[0].add_run(text)
        set_run_font(run, size=size, bold=True)
    for row in rows:
        cells = t.add_row().cells
        for cell, text in zip(cells, row):
            cell.text = ""
            run = cell.paragraphs[0].add_run(str(text))
            set_run_font(run, size=size)
    if widths:
        for row in t.rows:
            for cell, width in zip(row.cells, widths):
                cell.width = Cm(width)
    doc.add_paragraph().paragraph_format.space_after = Pt(2)
    return t


def figure(doc, name, caption_zh, caption_en, look_zh, look_en, width_cm=15.5):
    path = FIGURES / name
    if not path.exists():
        para(doc, f"[missing figure: {name}]", italic=True, colour=GREY)
        return
    doc.add_picture(str(path), width=Cm(width_cm))
    doc.paragraphs[-1].alignment = WD_ALIGN_PARAGRAPH.CENTER
    para(doc, caption_zh, size=9.5, bold=True, space_after=1, align=WD_ALIGN_PARAGRAPH.CENTER)
    para(doc, caption_en, size=9, italic=True, colour=GREY, space_after=6,
         align=WD_ALIGN_PARAGRAPH.CENTER)
    para(doc, "看什么：" + look_zh, size=10, space_after=2)
    para(doc, "What to look at: " + look_en, size=9.5, italic=True, colour=GREY, space_after=10)


def add_page_number_footer(doc):
    footer = doc.sections[0].footer
    p = footer.paragraphs[0]
    p.alignment = WD_ALIGN_PARAGRAPH.CENTER
    run = p.add_run()
    set_run_font(run, size=9, colour=GREY)
    fld = OxmlElement("w:fldSimple")
    fld.set(qn("w:instr"), "PAGE")
    run._element.addnext(fld)


# --------------------------------------------------------------------------
# document
# --------------------------------------------------------------------------

def build() -> Path:
    doc = Document()
    section = doc.sections[0]
    section.top_margin = Cm(2.2)
    section.bottom_margin = Cm(2.0)
    section.left_margin = Cm(2.2)
    section.right_margin = Cm(2.2)
    add_page_number_footer(doc)

    # ---- cover -----------------------------------------------------------
    para(doc, "", space_after=40)
    para(doc, "上肢助力外骨骼的仿真验证", size=26, bold=True, colour=ACCENT,
         align=WD_ALIGN_PARAGRAPH.CENTER, space_after=6)
    para(doc, "Simulation-Based Validation of an Upper-Limb Assist Exoskeleton",
         size=15, italic=True, colour=GREY, align=WD_ALIGN_PARAGRAPH.CENTER, space_after=24)
    para(doc, "右臂 · 肩侧 + 肘部 · 桌面固定式", size=12,
         align=WD_ALIGN_PARAGRAPH.CENTER, space_after=2)
    para(doc, "Right arm · shoulder + elbow · table-mounted", size=11, italic=True,
         colour=GREY, align=WD_ALIGN_PARAGRAPH.CENTER, space_after=40)
    para(doc, "MuJoCo 软件在环验证  |  MuJoCo software-in-the-loop validation",
         size=11, align=WD_ALIGN_PARAGRAPH.CENTER, space_after=4)
    para(doc, "2026-10-28", size=11, align=WD_ALIGN_PARAGRAPH.CENTER, space_after=40)
    para(doc, "说明：本文所有数字均为仿真模型假设下的结果，未与实物台架对标。",
         size=9.5, italic=True, colour=GREY, align=WD_ALIGN_PARAGRAPH.CENTER, space_after=1)
    para(doc, "Note: every number here is a model estimate; no bench comparison has been made.",
         size=9, italic=True, colour=GREY, align=WD_ALIGN_PARAGRAPH.CENTER)
    doc.add_page_break()

    # ---- abstract --------------------------------------------------------
    heading(doc, "摘要", "Abstract")
    bi(doc,
       "本项目为一套右臂助力外骨骼（肩侧电机 + 肘部电机，桌面固定式）建立了 MuJoCo 仿真环境，"
       "把固件中真实运行的控制代码编译成主机库接入仿真，用同一套代码、同一套参数比较"
       "“不穿戴 / 穿戴不通电 / 穿戴并助力”三种条件下的省力效果。人体使用 MyoArm 肌肉骨骼模型，"
       "外骨骼几何来自 CAD 导出件。结果显示：相对穿戴不通电，前三角肌激活下降 11.7–20.3%，"
       "与已发表的肩部外骨骼研究处于同一区间；相对裸臂的净收益约 3%，"
       "因为装置主要抵消的是自身重量。爆发动作的收益仅 4.5–6.4%，原因是峰值力矩以惯性为主。",
       "This project builds a MuJoCo environment for a right-arm assist exoskeleton "
       "(shoulder and elbow motors, table-mounted), compiles the real firmware control code "
       "into a host library and drives the simulation with that same code and the same "
       "parameters. Three conditions are compared: bare arm, worn but unpowered, and worn with "
       "assistance. The human arm is the MyoArm musculoskeletal model; the exoskeleton geometry "
       "comes from the CAD export. Against the unpowered case the anterior deltoid activation "
       "falls by 11.7–20.3%, the same range as published shoulder-exoskeleton studies, while the "
       "net gain against the bare arm is about 3% because the device mainly cancels its own "
       "weight. Burst movements gain only 4.5–6.4% because their peak torque is inertia-dominated.")

    # ---- 1 objective -----------------------------------------------------
    heading(doc, "1. 研究目标", "1. Objective")
    bi(doc, "① 用仿真印证固件控制程序确实省力，并给出量化结果；"
            "② 建立可在其中迭代优化的试验场，改控制逻辑后先在仿真里看结果，不必每次上实机。",
       "(1) Confirm in simulation that the firmware control law actually reduces effort, with "
       "numbers; (2) provide a test bed for iterating on the control logic without going to the "
       "bench every time.")

    # ---- 2 system --------------------------------------------------------
    heading(doc, "2. 系统与模型", "2. System and Model")
    bi(doc, "仿真由四部分组成：人体、外骨骼、桌子/支架、绑带。所有物理参数集中在 "
            "model/params.json，每项标注来源与状态（实测 / 估计 / 假设）。",
       "The scene has four parts: the human arm, the exoskeleton, the table and stand, and the "
       "straps. Every physical parameter lives in model/params.json with its source and status "
       "(measured, estimated, assumed).")
    table(doc,
          ["组成 / Part", "来源 / Source", "关键参数 / Key parameters"],
          [["人体 / Human", "MyoSim 0.2.3 myoarm_r",
            "站立、躯干固定；保留肩前屈与屈肘 2 个自由度；63 块肌肉"],
           ["外骨骼 / Exoskeleton", "CAD 导出包（只读副本）",
            "右臂外侧；沿调节轨加长到 290.5 mm 与人肘对齐；上臂 0.656 kg、前臂 0.177 kg"],
           ["桌子与支架 / Table", "代码构建",
            "桌面 0.90 m；竖杆 Ø14 mm 穿过夹持座孔（孔心由 STL 截面算出）"],
           ["绑带 / Straps", "两个软 weld",
            "上臂与前臂各一；solref 0.05 s、torquescale 0.03"]],
          widths=[3.4, 4.2, 8.4])

    # ---- 3 control -------------------------------------------------------
    heading(doc, "3. 控制架构与参数", "3. Control Architecture and Parameters")
    bi(doc, "固件控制代码（App/control/*.c 与 App/application/s5_site.c）编译成主机库，"
            "仿真每 10 ms 调用一次——与真机同一份代码、同一套参数，不是重新实现。"
            "人体侧有两种模型：理想关节力矩，以及肌肉驱动（计算力矩 → 静态优化 → 63 块肌肉）。",
       "The firmware control code (App/control/*.c and App/application/s5_site.c) is compiled into "
       "a host library that the simulation calls every 10 ms: the same code and the same "
       "parameters as on the device, not a re-implementation. The human side has two models: an "
       "ideal joint torque, and a muscle-driven arm (computed torque, static optimisation, 63 "
       "muscles).")
    table(doc,
          ["参数 / Parameter", "值 / Value", "说明 / Meaning"],
          [["肩重力模型", "1.64 N·m × sin(肩角)", "外骨骼肩侧自重模型"],
           ["肩增益", "1.5", "连人臂的一部分也一起补"],
           ["肩输出上限", "300 mNm 转子 = 2.4 N·m 关节", "约为电机额定的一半"],
           ["肩 Amplify", "300 mNm，>10°/s 触发，瞬时切换", "抬臂方向的额外增量"],
           ["肘重力模型", "10 A × 0.02 N·m/A × sin(肩+肘)", "肘部姿态相关助力"],
           ["肘 Amplify", "10 A，按运动方向叠加", "快速伸展（锤击）也助力"],
           ["输出周期", "10 ms（100 Hz）", "爆发响应的主要延迟来源"],
           ["控制库验证", "主机测试 125 项全部通过", "含合成、限幅、Amplify 时序用例"]],
          widths=[4.0, 6.0, 6.0])

    # ---- 4 experiment ----------------------------------------------------
    heading(doc, "4. 实验设计", "4. Experimental Design")
    heading(doc, "4.1 三种条件", "4.1 Conditions", level=2)
    table(doc,
          ["条件 / Condition", "含义 / Meaning"],
          [["C0", "裸臂：不绑外骨骼（关掉绑带约束）"],
           ["C1", "穿戴但外骨骼不通电：人还要承担外骨骼自重"],
           ["C2", "穿戴且由固件控制库驱动（本项目的真实控制代码）"]],
          widths=[4.0, 12.0])

    heading(doc, "4.2 姿势与动作", "4.2 Postures and Motions", level=2)
    table(doc,
          ["编号 / ID", "内容 / Content", "时长 / Duration", "用途 / Purpose"],
          [["A", "工作空间扫描：肩 5–60° × 肘 30–110°（12×9 网格）", "静态计算", "助力有效区"],
           ["P1", "器械与桌面平行：肩 20° / 肘 100°", "保持 60 s", "静态省力"],
           ["P4", "近身：肩 30° / 肘 110°", "保持 60 s", "静态省力"],
           ["P5", "远端：肩 60° / 肘 70°", "保持 60 s", "静态省力（重力最大）"],
           ["G1", "锤击：肘 100°→45°，180 ms，重复 10 次", "约 24 s", "爆发（肘主导）"],
           ["G2", "快速抬举：肩 5°→45°，250 ms，重复 10 次", "约 25 s", "爆发（肩主导）"],
           ["T1–T3", "静态保持 / 慢抬放 / 快抬放 / 自由变姿态", "10–25 s", "理想力矩模型对照（早期，脚本已清理）"]],
          widths=[2.2, 7.4, 2.6, 3.8])

    heading(doc, "4.3 指标", "4.3 Metrics", level=2)
    bullet(doc, "人体关节力矩（主指标）：肩、肘执行器输出的力矩，越低越省力。",
           "Human joint torque (primary): the torque the arm's own actuators apply; lower is better.")
    bullet(doc, "肌肉激活量（次指标）：对同一力矩做静态优化得到的 63 块肌肉激活之和与单块肌肉值。",
           "Muscle activation (secondary): the static-optimisation solution for that torque, reported "
           "as the sum over 63 muscles and per muscle.")
    bullet(doc, "爆发专用：峰值力矩、动作时间、稳态误差、人体正功（∫τ·q̇ dt）。",
           "Burst-specific: peak torque, movement time, steady-state error and positive human work.")
    bullet(doc, "交互与安全：绑带力、输出饱和占比、软限暂停与故障次数。",
           "Interaction and safety: strap force, output saturation, soft-limit pauses and faults.")

    heading(doc, "4.4 参数汇总", "4.4 Parameter Summary", level=2)
    table(doc,
          ["项目 / Item", "值 / Value"],
          [["人体", "70 kg / 1.75 m（缺省假设）；上臂 2.0 kg、前臂+手 1.78 kg"],
           ["桌高", "0.90 m"],
           ["外骨骼质量", "上臂组 0.656 kg、前臂组 0.177 kg（STL 体积 × ABS 1.1 g/cm³）"],
           ["绑带", "solref 0.05 s、torquescale 0.03"],
           ["仿真步长", "1 ms；控制 10 ms；记录 1 ms（爆发）/ 5 ms（静态）"],
           ["人体控制器", "PD + 计算力矩前馈 → 静态优化 → 肌肉激励"],
           ["软件版本", "MuJoCo 3.16.0、MyoSim 0.2.3、Python 3.13"]],
          widths=[4.0, 12.0])

    # ---- 5 results -------------------------------------------------------
    heading(doc, "5. 结果", "5. Results")

    heading(doc, "5.1 工作空间：助力在哪里有效", "5.1 Workspace: Where Assistance Helps", level=2)
    figure(doc, "05_workspace.png",
           "图 1  工作空间热力图：穿戴代价、助力收益、肌肉激活收益",
           "Figure 1  Workspace maps: cost of wearing, assistance benefit, activation benefit",
           "左图是“穿上不通电比裸臂多费多少力”，中间是“助力能收回多少”，右边是激活量的收益。"
           "当前默认参数下穿戴代价均值 +13.2%、助力收益均值 +13.7%、激活收益均值 +6.9%。"
           "抬臂越高、肘越弯，助力越有效（肩 60°/肘 70–110° 处 19–24%）；"
           "低抬臂（肩 10–15°）区域激活收益为负（多为 −3…−5%，个别 −14%）——助力反而让肌肉更累，"
           "说明该姿态下补偿“帮过头”，需要在台架小角度复核。激活基线 <0.2 的格子标 n/a，"
           "因为基线太小时百分比会被放大成假象。",
           "The left panel is how much more effort the unpowered device costs, the middle how much "
           "the assist gives back, the right the activation benefit. With the current defaults the "
           "means are +13.2% cost, +13.7% benefit and +6.9% activation benefit. Assistance works "
           "better the higher and more flexed the arm is (19–24% at 60° shoulder, 70–110° elbow), "
           "while at low elevation (10–15° shoulder) the activation benefit is negative (mostly "
           "−3 to −5%, one cell −14%): there the assist over-compensates, and that region should "
           "be checked on the bench. Cells with an unpowered activation below 0.2 are marked n/a, "
           "because a percentage of such a small baseline is misleading.")

    heading(doc, "5.2 静态姿势：三种条件的直接对照", "5.2 Static Postures: Three Conditions", level=2)
    figure(doc, "02_static.png",
           "图 2  三个手术姿势的肌肉激活与人体力矩",
           "Figure 2  Muscle activation and human torque for the three surgical postures",
           "橙色（穿戴不通电）总是最高，说明外骨骼自重确实压在人臂上；"
           "蓝色（助力）回落到灰色（裸臂）附近甚至更低。"
           "以 P5 为例：肩力矩 8.93 → 10.42 → 8.35 N·m，助力把穿戴带来的 1.49 N·m 全部收回，"
           "还多补了 0.58 N·m。",
           "Orange (worn, unpowered) is always the highest, so the exoskeleton's own weight really "
           "does load the arm; blue (assisted) comes back to the grey bare-arm level or below. For "
           "P5 the shoulder torque goes 8.93 → 10.42 → 8.35 N·m: the assist removes the whole "
           "1.49 N·m the device added and another 0.58 N·m of the arm's own load.")

    heading(doc, "5.3 论文口径：前三角肌", "5.3 Paper Convention: Anterior Deltoid", level=2)
    figure(doc, "03_deltoid.png",
           "图 3  前三角肌激活（论文常用口径）与降幅",
           "Figure 3  Anterior deltoid activation and its reduction",
           "肩部外骨骼文献普遍只报三角肌。左图三条柱子的关系与图 2 一致；"
           "右图把“相对穿戴不通电”和“相对裸臂”分开：相对穿戴降低 11.7–20.3%，"
           "相对裸臂只有约 3%。这正是与文献比较时应该用的数字，也是解释“为什么看起来不明显”的关键。",
           "Shoulder-exoskeleton papers usually report the deltoid only. The left panel mirrors "
           "Figure 2; the right panel separates the two baselines: 11.7–20.3% against the unpowered "
           "case but only about 3% against the bare arm. The first is the number to compare with "
           "the literature, the second explains why the effect can look small.")

    heading(doc, "5.4 爆发动作", "5.4 Burst Movements", level=2)
    figure(doc, "04_burst.png",
           "图 4  爆发动作的肌肉激活与关节角速度",
           "Figure 4  Muscle activation and joint velocity in the bursts",
           "上排是激活量、下排是角速度，左列锤击（肘）、右列快速抬举（肩）。"
           "G1 中蓝色（助力）全程压在橙色下方，说明助力确实参与了动作；"
           "G2 在加速段三条曲线几乎重合——爆发以惯性为主，重力补偿帮不上；"
           "而爆发结束后蓝色反而高于橙色，对应“放下阶段被助力顶住”的现象（见第 7 节）。",
           "Top row activation, bottom row velocity; the left column is the elbow strike, the right "
           "the shoulder lift. In G1 the blue curve stays below orange throughout, so the assist "
           "really takes part; in G2 the three curves overlap during the acceleration because the "
           "burst is inertia-dominated, and after the burst blue rises above orange, which is the "
           "assist pushing during lowering (see Section 7).")

    table(doc,
          ["指标 / Metric", "G1 锤击 / strike", "G2 快速抬举 / lift"],
          [["峰值力矩 C0/C1/C2 (N·m)", "4.69 / 4.87 / 4.56", "44.7 / 46.5 / 46.1"],
           ["人体正功 C0/C1/C2 (J)", "18.8 / 19.5 / 18.7", "119.6 / 123.0 / 124.5"],
           ["外骨骼正功 (J)", "2.0", "13.3"],
           ["动作时间 (ms)", "430 / 435 / 435", "225（参考 250）"],
           ["稳态误差 (°)", "1.2", "0.2"]],
          widths=[6.0, 5.0, 5.0])

    heading(doc, "5.5 总览图", "5.5 Overview", level=2)
    figure(doc, "01_overview.png",
           "图 5  静态、三角肌、爆发三张结果图拼合",
           "Figure 5  Static, deltoid and burst results combined",
           "用于作品集的一页式总览：上排是静态姿势的两种口径，下排是爆发的时序曲线。",
           "A one-page summary for a portfolio: the static postures in two conventions on top, the "
           "burst time series below.")

    heading(doc, "5.6 与文献对比", "5.6 Comparison with the Literature", level=2)
    table(doc,
          ["研究 / Study", "报告的数字 / Reported", "与本项目的关系 / Relation"],
          [["Frontiers Bioeng. 2024（MIS 肩外骨骼）", "三角肌前束 −21.0%、中束 −14.6%、后束 −8.3%",
            "同一区间（本机 −11.7…−20.3%，相对穿戴）"],
           ["Ergonomics 2026（模拟腹腔镜，5 个姿势）", "前三角肌各姿势均有下降", "同上量级"],
           ["Applied Ergonomics 2024（助力 60–100%）", "抬臂最多 −61~64%；下放阶段伸肌 +127%",
            "方向一致，但本机助力幅值更小"],
           ["ArmeoPower（整臂完全失重）", "峰值关节力矩 −74.8%", "远高于本机，属“全支撑”设备"]],
          widths=[5.6, 6.4, 4.0])

    # ---- 6 discussion ----------------------------------------------------
    heading(doc, "6. 讨论：为什么“相对裸臂”只有约 3%", "6. Discussion: Why Only ~3% Against the Bare Arm")
    bullet(doc, "助力幅值只覆盖一部分：肩上限 2.4 N·m，而 P5 人臂需要 8.93 N·m，覆盖约 23%。",
           "The assist covers only part of the load: the shoulder cap is 2.4 N·m against 8.93 N·m "
           "needed at P5, about 23%.")
    bullet(doc, "外骨骼自身很重：0.83 kg 挂在 3.78 kg 的手臂上，给手臂增加约 22% 的负担。",
           "The device is heavy relative to the arm: 0.83 kg on a 3.78 kg arm, about 22% more.")
    bullet(doc, "两者相抵，净收益自然小——这是设计定位（助力而非全支撑）决定的，不是控制逻辑问题。",
           "The two cancel out, so the net gain is small by construction: the device is designed to "
           "assist, not to carry the whole arm.")
    bi(doc, "爆发收益小的原因已定量：250 ms 动作的峰值惯性项 80.75 N·m，重力项只有 7.61 N·m（8.6%）；"
            "本机 4.6 N·m 的极限仅为惯性项的 6%。把 Amplify 的时序从 200 ms 斜坡改为瞬时切换后，"
            "增量介入时刻由 241 ms 提前到 121 ms，肘部爆发收益从 4.5% 提升到 6.4%。",
       "The burst gain is small for a quantified reason: at 250 ms the peak inertial term is "
       "80.75 N·m against 7.61 N·m of gravity (8.6%), and the device's 4.6 N·m ceiling is only 6% "
       "of the inertial term. Making Amplify switch instantly instead of ramping over 200 ms moved "
       "its engagement from 241 ms to 121 ms and raised the elbow strike gain from 4.5% to 6.4%.")

    # ---- 7 limitations ---------------------------------------------------
    heading(doc, "7. 局限性", "7. Limitations")
    bullet(doc, "模型只有两个自由度：肩前屈与屈肘。肩外展、内外旋、前臂旋转未建模，"
                "因此三角肌中束、冈上/冈下肌在模型中激活为 0，不能用本报告评估这些肌肉。",
           "Only two degrees of freedom: shoulder flexion and elbow flexion. Abduction, internal "
           "and external rotation and forearm pronation are not modelled, so the middle deltoid, "
           "supraspinatus and infraspinatus read zero and cannot be assessed here.")
    bullet(doc, "“人”是简化控制器：PD + 计算力矩 + 最小激活优化，不是真实运动控制模型。"
                "真人会协同收缩、有反射与疲劳，被助力后会改变策略。因此数字适合作相对比较，"
                "不是真人用力的绝对预测。",
           "The human is a simplified controller: PD plus computed torque plus a minimum-activation "
           "optimisation, not a real motor-control model. Real people co-contract, have reflexes "
           "and fatigue, and change strategy when assisted. The numbers support relative "
           "comparisons, not absolute predictions of human effort.")
    bullet(doc, "物理参数多为估计：外骨骼质量按 STL 体积 × ABS 密度、绑带软度与关节阻尼为假设、"
                "人体尺寸取 70 kg / 1.75 m 缺省值、桌高 0.90 m 为常用值。",
           "Most physical parameters are estimates: exoskeleton mass from STL volume times ABS "
           "density, strap compliance and joint damping assumed, the human at the default 70 kg / "
           "1.75 m, the table at a typical 0.90 m.")
    bullet(doc, "没有台架对标：与实物日志的比对（原 M5）已取消，因此这些数字没有经过实测校准。",
           "No bench comparison: the planned comparison with bench logs (M5) was cancelled, so "
           "these numbers are not calibrated against measurements.")
    bullet(doc, "镜像已编译未刷写：固件镜像通过构建与 125 项主机测试，但没有上板；"
                "100 Hz 输出下的 CAN 稳定性、10 A 肘电流的热裕量、编码器噪声是否误触发 Amplify，"
                "都还没有验证。",
           "The images are built but not flashed: they pass the build and 125 host tests but have "
           "never run on the board; CAN stability at 100 Hz, the thermal margin at 10 A elbow "
           "current, and whether encoder noise false-triggers Amplify are all unverified.")
    bullet(doc, "放下阶段未处理：按阶段拆分显示抬臂 −6.8%、保持 −14.2%，但放下阶段 +10.5%"
                "（重力基线仍向上推）。这与文献报告的现象一致，本轮未做方向相关基线。",
           "The lowering phase is not handled: splitting a cycle gives −6.8% while raising and "
           "−14.2% while holding, but +10.5% while lowering, because the gravity baseline keeps "
           "pushing up. The literature reports the same effect; no direction-dependent baseline "
           "was implemented in this round.")
    bullet(doc, "单一虚拟受试者：没有跨受试者的变异性分析（原计划的虚拟队列已取消）。",
           "A single virtual subject: no across-subject variability analysis (the planned virtual "
           "cohort was dropped).")

    # ---- 8 reproducibility ----------------------------------------------
    heading(doc, "8. 复现", "8. Reproducibility")
    para(doc, "所有结果可由以下命令重跑（在 firmware/Exoskeleton_F446 目录）：", space_after=2)
    para(doc, "All results can be regenerated with the commands below (from firmware/Exoskeleton_F446):",
         size=9.5, italic=True, colour=GREY, space_after=6)
    for cmd in (
        "build\\sim-venv\\Scripts\\python.exe sim\\mujoco_arm\\scripts\\report_muscle_all.py",
        "build\\sim-venv\\Scripts\\python.exe sim\\mujoco_arm\\scripts\\workspace_scan.py",
        "build\\sim-venv\\Scripts\\python.exe sim\\mujoco_arm\\scripts\\check_static.py",
        "build\\sim-venv\\Scripts\\python.exe sim\\mujoco_arm\\docs\\make_report_docx.py",
    ):
        p = para(doc, cmd, size=9, space_after=2)
        p.paragraph_format.left_indent = Cm(0.6)
        for run in p.runs:
            run.font.name = "Consolas"
    para(doc, "模型与参数：sim/mujoco_arm/model/params.json；控制参数：App/application/s5_site.c；"
              "报告与图：sim/mujoco_arm/docs/。", size=9.5, space_after=2)
    para(doc, "Model and parameters: sim/mujoco_arm/model/params.json; control parameters: "
              "App/application/s5_site.c; report and figures: sim/mujoco_arm/docs/.",
         size=9, italic=True, colour=GREY)

    # ---- 9 figure index --------------------------------------------------
    heading(doc, "9. 图表索引", "9. Figure Index")
    table(doc,
          ["文件 / File", "内容 / Content"],
          [["figures/01_overview.png", "总览：静态 + 三角肌 + 爆发 / overview"],
           ["figures/02_static.png", "静态姿势：激活与力矩 / static postures"],
           ["figures/03_deltoid.png", "前三角肌口径 / anterior deltoid"],
           ["figures/04_burst.png", "爆发时序 / burst time series"],
           ["figures/05_workspace.png", "工作空间热力图 / workspace maps"],
           ["figures/06–08_p1/p4/p5.png", "三个姿势的场景图 / scene stills"]],
          widths=[6.0, 10.0])

    # ---- 10 implementation references ------------------------------------
    heading(doc, "10. 程序实现参考", "10. Implementation References")
    bi(doc, "本项目遵循“先复用现有实现，再参考开源项目，最后才自行编写”的顺序。"
            "下表列出实际读过的源码或文档，以及我们拿走了什么、改了什么。"
            "只列真正用到的，未使用的参考不列入。",
       "The project follows one order: reuse our own code first, then the referenced "
       "open-source projects, and only then write new code. The table lists the sources "
       "actually read, what was taken and what had to be adapted. Only what was used is listed.")
    table(doc,
          ["模块 / Module", "参考来源 / Source", "拿走了什么 / Taken", "改了什么 / Adapted"],
          [["动作放大（Amplify）", "ExoKit `AmplifyReaction::runLoop()`",
            "速度触发、换向暂停（dirChangeSuspensionTime）的状态机",
            "改为肩、肘两通道；增量为转子 N·m / C620 A；改为瞬时切换；新增正功率门控（默认关）"],
           ["人体模型", "MyoSim 0.2.3 `myoarm_r`（Apache-2.0）",
            "骨骼几何、关节定义、63 块肌肉与力臂",
            "只保留肩前屈与屈肘两个自由度；抬臂平面烘焙进刚体姿态；去掉肌肉（度量模型保留）"],
           ["外骨骼几何与运动链", "本项目 CAD 导出包（OrthoEase）",
            "18 个 STL、两级转动关节、绑定点 site",
            "只读副本；质量按 STL 体积×ABS 重算；沿调节轨加长 34.8 mm 与人肘对齐；位置执行器换力矩执行器"],
           ["仿真引擎", "MuJoCo 3.16.0",
            "刚体动力学、等式约束、`mj_fullM`/`qfrc_bias`/`qfrc_passive`/`qfrc_constraint`",
            "无（只调用）"],
           ["肌肉控制方法", "MyoSuite 教程 `3.3_Inverse_Dynamics`、`3.4_Computed_Muscle_Control`；issue #337 维护者建议",
            "“期望加速度 → 所需广义力 → 静态优化分配肌肉”的流程",
            "`mj_inverse` 在本版本不填 `qfrc_inverse`（除非开离散标志），改用计算力矩公式自行计算"],
           ["桌面与场景写法", "MyoSuite/RoboHive `myoarm_relocate.xml`",
            "静态桌子 box + contype/conaffinity 写法",
            "桌子、方块、竖杆按本项目尺寸重建；竖杆轴线由 01B 夹持座 STL 截面算出"],
           ["控制律结构", "OpenSim CMC（Thelen 2003）",
            "“PD 求期望加速度 + 静态优化分配”的分层",
            "在 MuJoCo 中实现；无残差执行器"],
           ["固件底座与驱动", "STM32CubeF4、BetaRobot、UICRM-Embedded、Rhino-Pi-X1（见开源借鉴索引第 3/5/8/9 行）",
            "HAL 工程组织、CAN 收发与过滤器、GDS68/C620 报文解析骨架",
            "沿用既有固件，本次未改驱动层"],
           ["报告工具", "python-docx 1.2.0、matplotlib 3.10.9、numpy 2.4.6",
            "文档生成与绘图", "无"]],
          widths=[3.2, 4.4, 4.4, 5.0], size=8)

    # ---- 11 references ---------------------------------------------------
    heading(doc, "11. 参考文献", "11. References")
    refs = [
        ("[1] Choi H.S., Lee S.J., In H. Development and clinical validation of passive shoulder "
         "exoskeleton with novel gravity compensation mechanism for stabilizing arm tremor of "
         "surgeons during minimally invasive surgery. Front. Bioeng. Biotechnol. 12 (2024). "
         "DOI 10.3389/fbioe.2024.1418148"),
        ("[2] Barbieri D.F., Gonzales A., Srinivasan D., Raghuraman R.N., Carbonell A.M., Cha J.S. "
         "Evaluation of shoulder-support exoskeletons during simulated laparoscopic surgery across "
         "representative shoulder postures. Ergonomics (2026). DOI 10.1080/00140139.2026.2721391"),
        ("[3] Ramella G., Grazi L., et al. Evaluation of antigravitational support levels provided by "
         "a passive upper-limb occupational exoskeleton in repetitive arm movements. "
         "Applied Ergonomics (2024). DOI 10.1016/j.apergo.2024.104226"),
        ("[4] J Safety Research (2024). Evaluation of a passive arm-support exoskeleton for surgical "
         "team members: results from live surgeries. DOI 10.1016/j.jsr.2024.02.003"),
        ("[5] Passive shoulder occupational exoskeleton reduces shoulder muscle coactivation in "
         "repetitive arm movements. Scientific Reports 14 (2024). DOI 10.1038/s41598-024-78090-2"),
        ("[6] Modulation of shoulder muscle and joint function using a powered upper-limb exoskeleton "
         "(ArmeoPower). J. Biomech. (2018). PubMed 29506759."),
        ("[7] Thelen D.G., Anderson F.C., Delp S.L. Generating dynamic simulations of movement using "
         "computed muscle control. J. Biomech. 36(3):321-328 (2003). DOI 10.1016/S0021-9290(02)00432-3"),
        ("[8] Maffiuletti N.A., et al. Rate of force development: physiological and methodological "
         "considerations. Eur. J. Appl. Physiol. 116 (2016). DOI 10.1007/s00421-016-3346-6"),
        ("[9] ASTM F3517-21. Standard Practice for Movement Tests When Using an Exoskeleton. ASTM "
         "International (2021)."),
        ("[10] MuJoCo 3.16.0, Google DeepMind. https://github.com/google-deepmind/mujoco"),
        ("[11] MyoSim 0.2.3 / MyoSuite, MyoHub. https://github.com/MyoHub/myo_sim ; "
         "https://github.com/MyoHub/myosuite"),
        ("[12] ExoKit, HCI Lab Saarland. https://github.com/HCI-Lab-Saarland/ExoKit"),
        ("[13] MyoSuite issue #337（肌肉模型的反解建议）. "
         "https://github.com/MyoHub/myosuite/issues/337"),
    ]
    for ref in refs:
        p = para(doc, ref, size=9, space_after=3)
        p.paragraph_format.left_indent = Cm(0.8)
        p.paragraph_format.first_line_indent = Cm(-0.8)

    # ---- 12 work performed ----------------------------------------------
    heading(doc, "12. 本项目完成的工作", "12. Work Performed")
    bi(doc, "以下按“仿真环境、控制接入、实验、固件、文档”五块列出本次实际完成的内容，"
            "以及明确没有做的事。",
       "The list below covers what was actually delivered in five blocks (simulation "
       "environment, control integration, experiments, firmware, documentation) and what was "
       "deliberately not done.")
    table(doc,
          ["类别 / Area", "完成内容 / Delivered"],
          [["仿真环境",
            "MuJoCo 场景：MyoArm 人体 + CAD 外骨骼 + 0.90 m 桌子与竖杆 + 两条软绑带；"
            "肩/肘轴对齐、调节轨加长、竖杆穿过夹持座孔；参数集中在 params.json 并标注来源"],
           ["控制接入",
            "把固件 App/control 与 s5_site.c 编译成主机库，仿真按 10 ms 调用；"
            "抽出 s5_assist_compose 供固件与仿真共用；新增 125 项主机测试"],
           ["人体模型",
            "理想关节力矩版；肌肉驱动版（计算力矩前馈 + 静态优化 + 63 块肌肉，含激活动力学）"],
           ["实验",
            "工作空间扫描（12×9）；三个手术姿势各 60 s；锤击与快速抬举两个爆发动作；"
            "T1–T3 理想力矩对照（早期，脚本已清理）；每种条件 C0/C1/C2"],
           ["主要结果",
            "前三角肌相对穿戴降低 11.7–20.3%；相对裸臂约 3%；爆发 6.4%（肘）/0.7%（肩）；"
            "查明放下阶段 +10.5% 的根因；给出惯性/重力分解（250 ms 时重力仅占 8.6%）"],
           ["固件改动",
            "肘幅值 2 A→10 A；肩增益 1.5、上限 300 mNm（新编译选项）；"
            "肘通道新增 Amplify；Amplify 改瞬时切换；输出周期 50→10 ms；"
            "正功率门控（默认关）。镜像已编译（142392 B，0 警告），**未刷写**"],
           ["工具与文档",
            "8 个脚本（模型、检查、实验、报告、扫描）；一键出图；"
            "Markdown 与中英对照 Word 报告；schedule 1.2–1.4 节与逐日开发日志"],
           ["未做 / Not done",
            "台架对标（M5 取消）；镜像刷写与实机验证；方向相关重力基线；"
            "跨受试者变异性；肩外展/旋转自由度"]],
          widths=[3.0, 13.0], size=8.5)

    doc.save(OUT)
    return OUT


if __name__ == "__main__":
    import sys
    if "--out" in sys.argv:
        OUT = Path(sys.argv[sys.argv.index("--out") + 1])
    path = build()
    print(f"wrote {path}")
