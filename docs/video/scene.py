"""ESPPRC explainer, rendered with 3Blue1Brown's ManimGL 1.7.2.

The graph answer is verified by demo.py. Label and bucket diagrams illustrate
the algorithm; they are not an instrumented trace or a performance benchmark.
"""
from pathlib import Path
import json
import manimpango

# Register the bundled, unmodified LaTeX font family before making any Text.
for font_path in (Path(__file__).parent/'fonts').glob('*.otf'):
    if not manimpango.register_font(str(font_path)):
        raise RuntimeError(f'Cannot register bundled font: {font_path}')

from manimlib import *
from demo import POSITIONS, EDGES, verify
from subtitles import normalize_subtitles

BG = '#0B1220'
PANEL = '#142238'
INK = '#F0F5FC'
MUTED = '#A0B2CC'
EDGE = '#3E5270'
CYAN = '#5ADDE7'
GREEN = '#75E5B6'
RED = '#FF829A'
PURPLE = '#B6A3FF'
GOLD = '#FFD184'

def text(s, size=32, color=INK, width=6.9, **kwargs):
    result = Text(s, font='Latin Modern Roman', font_size=size,
                  t2f={'≤':'Latin Modern Math','✓':'Latin Modern Math'},
                  **kwargs).set_color(color)
    if result.get_width() > width:
        result.set_width(width)
    return result

def panel(width=6.7, height=1.0, color=EDGE):
    return RoundedRectangle(width=width, height=height, corner_radius=.17,
                            fill_color=PANEL, fill_opacity=1,
                            stroke_color=color, stroke_width=1.4)

class ESPPRCShowcase(Scene):
    output_path = Path('.')

    def construct(self):
        self.example = verify()
        self.captions = []
        self.slide = VGroup()
        self.footer = text('ESPPRC  /  C++ + Python', 18, MUTED).move_to([0, -3.65, 0])
        self.rule = Line([-3.45, -3.34, 0], [3.45, -3.34, 0], stroke_color=EDGE, stroke_width=1)
        self.add(self.footer, self.rule)
        self.intro()
        self.problem()
        self.bidirectional()
        self.dominance()
        self.buckets()
        self.library()
        self.credits()
        self.captions.append((self.time, ''))
        self.write_captions()
        (self.output_path/'demo_verified.json').write_text(json.dumps(self.example, indent=2)+'\n')

    def cue(self, sentence):
        self.captions.append((self.time, sentence))

    def put(self, *objects):
        self.slide.add(*objects)
        return objects

    def next_slide(self):
        self.play(FadeOut(self.slide, shift=.12*UP), run_time=.5)
        self.slide = VGroup()

    def heading(self, number, title, subtitle=None):
        kicker=text(f'{number}  /  THE ELEMENTARY SHORTEST PATH PROBLEM',16,CYAN).move_to([0,3.53,0])
        title=text(title,42).move_to([0,2.94,0])
        self.put(kicker,title)
        self.play(FadeIn(kicker),FadeIn(title,shift=.15*UP),run_time=.6)
        if subtitle:
            sub=text(subtitle,25,MUTED).move_to([0,2.32,0])
            self.put(sub); self.play(FadeIn(sub),run_time=.35)

    def snapshot(self, name):
        self.update_frame(dt=0)
        self.camera.get_image().save(self.output_path/f'{name}.png')

    def intro(self):
        self.cue('Find the cheapest route that respects resource limits and never repeats a customer.')
        eyebrow=text('ROUTING  •  SCHEDULING  •  COLUMN GENERATION',18,CYAN).move_to([0,2.5,0])
        title=text('ESPPRC',88).move_to([0,1.35,0])
        subtitle=text('The cheapest route\nhas constraints.',43).move_to([0,-.12,0])
        subtitle.set_color_by_text('constraints.',CYAN)
        tag=text('Elementary shortest path\nwith resource constraints',25,MUTED).move_to([0,-1.55,0])
        dots=VGroup(*[Dot([x,-2.52,0],radius=.09).set_color(CYAN) for x in [-2,-1,0,1,2]])
        lines=VGroup(*[Line([x,-2.52,0],[x+1,-2.52,0],stroke_color=EDGE,stroke_width=3) for x in [-2,-1,0,1]])
        self.put(eyebrow,title,subtitle,tag,lines,dots)
        self.play(FadeIn(eyebrow),FadeIn(title,shift=.25*UP),run_time=.8)
        self.play(FadeIn(subtitle),FadeIn(tag),ShowCreation(lines),FadeIn(dots),run_time=1)
        self.wait(2.5)
        self.snapshot('poster')
        self.next_slide()

    def graph(self, y=.0, labels=True):
        nodes={}
        points={n:np.array([x, yy+y,0]) for n,(x,yy) in POSITIONS.items()}
        for n,p in points.items():
            c=CYAN if n=='S' else PURPLE if n=='T' else INK
            nodes[n]=VGroup(Circle(radius=.23,fill_color=BG,fill_opacity=1,stroke_color=c,stroke_width=2),text(n,26,c)).move_to(p)
        arcs={}; values=VGroup()
        for u,v,c,t in EDGES:
            a=Arrow(points[u],points[v],buff=.26,thickness=2,tip_width_ratio=3.5).set_color(EDGE)
            arcs[u,v]=a
            if labels:
                # Only annotate the two illustrated routes; the full graph is in demo.py.
                if (u,v) in [('S','A'),('A','C'),('C','T'),('S','B'),('B','D'),('D','T')]:
                    dy=.22 if v in ('A','C') or u=='C' else -.22
                    lbl=text(f'{c} / {t}',19,MUTED).move_to((points[u]+points[v])/2+[0,dy,0])
                    values.add(lbl)
        group=VGroup(*arcs.values(),*nodes.values(),values)
        return group,nodes,arcs

    def highlight(self, arcs, path, color):
        return VGroup(*[arcs[u,v].copy().set_color(color).set_stroke(width=5) for u,v in zip(path,path[1:])])

    def problem(self):
        self.heading('01','Cheapest is not always feasible')
        self.cue('The travel-time budget is eight units. Payload is limited to two units; each customer uses one.')
        caps=VGroup(text('Time ≤ 8',28,GOLD),text('Payload ≤ 2',28,GOLD)).arrange(RIGHT,buff=.55).move_to([0,2.05,0])
        graph,nodes,arcs=self.graph(y=.15)
        legend=text('Shown arc labels: cost / time',19,MUTED).move_to([0,-1.48,0])
        elementary=text('Each customer at most once. Each uses 1 payload unit.',20,MUTED).move_to([0,-1.87,0])
        self.put(caps,graph,legend,elementary)
        self.play(FadeIn(caps),ShowCreation(graph),FadeIn(legend),FadeIn(elementary),run_time=1)
        cheap=self.highlight(arcs,['S','A','C','T'],RED)
        card=panel(height=.8).move_to([0,-2.65,0])
        bad=text('Cost 3   •   Time 12 > 8   •   Infeasible',25,RED).move_to(card)
        self.put(cheap,card,bad)
        self.cue('The cheapest path costs three, but takes twelve time units. It is infeasible.')
        self.play(ShowCreation(cheap),FadeIn(card),FadeIn(bad),run_time=1.2)
        self.wait(2.6)
        best=self.highlight(arcs,self.example['best']['path'],GREEN)
        good=text('Cost 6   •   Time 6   •   Payload 2   ✓',25,GREEN).move_to(card)
        self.put(best,good)
        self.cue('The best feasible path is S–B–D–T: cost six, time six, payload two. Verified by the solver and exhaustive enumeration.')
        self.play(FadeOut(cheap),FadeOut(bad),ShowCreation(best),FadeIn(good),run_time=1.2)
        self.wait(3)
        self.snapshot('route')
        self.next_slide()

    def bidirectional(self):
        self.heading('02','Search from both ends','Grow partial paths. Join compatible labels.')
        self.cue('Bidirectional labeling grows paths from the start and from the destination, then joins compatible partial paths.')
        graph,nodes,arcs=self.graph(y=.1,labels=False)
        self.put(graph);self.play(FadeIn(graph),run_time=.6)
        fwd=self.highlight(arcs,['S','B'],CYAN)
        bwd=self.highlight(arcs,['D','T'],PURPLE)
        forward=text('FORWARD',23,CYAN).move_to([-1.65,-1.7,0])
        backward=text('BACKWARD',23,PURPLE).move_to([1.65,-1.7,0])
        self.put(fwd,bwd,forward,backward)
        self.play(ShowCreation(fwd),ShowCreation(bwd),FadeIn(forward),FadeIn(backward),run_time=1.4)
        f_dot=Dot(nodes['S'].get_center(),radius=.1).set_color(CYAN)
        b_dot=Dot(nodes['T'].get_center(),radius=.1).set_color(PURPLE)
        self.put(f_dot,b_dot);self.add(f_dot,b_dot)
        self.play(f_dot.animate.move_to(nodes['B']),b_dot.animate.move_to(nodes['D']),run_time=1.3)
        self.remove(f_dot,b_dot)
        join=self.highlight(arcs,['B','D'],GREEN)
        complete=text('Join only when resources and elementarity allow.',24,GREEN).move_to([0,-2.55,0])
        self.put(join,complete)
        self.play(ShowCreation(join),FadeIn(complete),run_time=.9)
        self.wait(2.3)
        self.snapshot('bidirectional')
        self.next_slide()

    def dominance(self):
        self.heading('03','Keep the useful labels','Same node. Same visited set.')
        self.cue('At the same node, with the same visited set, a label with no higher cost or resource use can dominate another.')
        columns=text('LABEL          COST          TIME          LOAD',22,MUTED).move_to([0,1.4,0])
        top=panel(height=.9,color=GREEN).move_to([0,.55,0])
        lower=panel(height=.9).move_to([0,-.6,0])
        a=text('A                 4                 3                 2',28,GREEN).move_to(top)
        b=text('B                 7                 5                 2',28,INK).move_to(lower)
        self.put(columns,top,lower,a,b)
        self.play(FadeIn(columns),FadeIn(top),FadeIn(a),FadeIn(lower),FadeIn(b),run_time=.8)
        self.wait(2)
        slash=Line([-2.85,-.6,0],[2.85,-.6,0],stroke_color=RED,stroke_width=3)
        note=text('A leaves at least as much room for every completion.',24,GREEN).move_to([0,-1.75,0])
        note2=text('Discard B. Preserve the optimal route.',28).move_to([0,-2.45,0])
        self.put(slash,note,note2)
        self.play(ShowCreation(slash),b.animate.set_opacity(.35),FadeIn(note),FadeIn(note2),run_time=.8)
        self.wait(2.3)
        self.next_slide()

    def buckets(self):
        self.heading('04','Spend less time comparing','Adaptive resource buckets organize the labels.')
        self.cue('Adaptive buckets group labels by resource use. Conservative summaries skip impossible comparisons; surviving labels still receive exact checks.')
        centers=[-2.25,0,2.25]
        ranges=['short','medium','longer']
        boxes=VGroup();dots=VGroup()
        for idx,x in enumerate(centers):
            rect=panel(width=2.02,height=1.65,color=CYAN if idx==0 else EDGE).move_to([x,.45,0])
            caption=text(ranges[idx],23,MUTED).move_to([x,1.55,0])
            boxes.add(rect,caption)
            for j in range(8):
                dots.add(Dot([x-.6+(j%4)*.4,.75-(j//4)*.48,0],radius=.07).set_color(CYAN if idx==0 else PURPLE))
        axis=Arrow([-3.25,-.7,0],[3.25,-.7,0],buff=0,thickness=1.5).set_color(MUTED)
        axislabel=text('Accumulated resource →',22,MUTED).move_to([0,-1.12,0])
        self.put(boxes,dots,axis,axislabel)
        self.play(LaggedStartMap(FadeIn,boxes,lag_ratio=.1),FadeIn(dots),ShowCreation(axis),FadeIn(axislabel),run_time=1)
        self.wait(1.3)
        note=text('Screen groups. Check surviving labels.',30,CYAN).move_to([0,-1.95,0])
        sub=text('Used in extension, dominance and joining.',24,MUTED).move_to([0,-2.55,0])
        self.put(note,sub)
        self.play(FadeIn(note),FadeIn(sub),run_time=.7)
        self.wait(2.8)
        self.snapshot('buckets')
        self.next_slide()

    def library(self):
        self.heading('05','A reusable pricing library','C++ core. Python interface. MIT licensed.')
        self.cue('ESPPRC extends cspy with bucket screening, faster joins, SRI-aware costs and optional parallel preparation. Serial remains the default.')
        code=Text('from espprc import solve_espprc\n\nresult = solve_espprc(graph, **options)\n\nresult["cost"]  # 6.0',font='Latin Modern Mono',font_size=24).set_color(CYAN)
        if code.get_width()>6:code.set_width(6)
        card=panel(height=1.8).move_to([0,.75,0]);code.move_to(card)
        features=VGroup(text('SRI-aware completed-route costs',28),
                        text('Optional OpenMP preparation',28),
                        text('Tests + seven portable fixtures',28)).arrange(DOWN,buff=.32).move_to([0,-1.3,0])
        fine=text('Parallel speedups depend on the workload.',20,MUTED).move_to([0,-2.7,0])
        self.put(card,code,features,fine)
        self.play(FadeIn(card),FadeIn(code),run_time=.8)
        self.play(LaggedStartMap(FadeIn,features,lag_ratio=.35),FadeIn(fine),run_time=1.2)
        self.wait(3.1)
        self.snapshot('library')
        self.next_slide()

    def credits(self):
        self.cue('Built on cspy by David Torres Sanchez. Subset-row inequalities: Jepsen, Petersen, Spoorendonk and Pisinger, 2008. Animation created with 3Blue1Brown’s ManimGL.')
        title=text('Built on open research.',44).move_to([0,2.95,0])
        cspy=text('Solver foundation: cspy',28,CYAN).move_to([0,2,0])
        author=text('David Torres Sanchez  •  JOSS (2020)',23,MUTED).move_to([0,1.56,0])
        doi=text('doi.org/10.21105/joss.01655',23).move_to([0,1.12,0])
        sri=text('Subset-row inequalities',28,GOLD).move_to([0,.32,0])
        paper=text('Jepsen, Petersen, Spoorendonk & Pisinger (2008)',21,MUTED).move_to([0,-.15,0])
        doi2=text('doi.org/10.1287/opre.1070.0449',23).move_to([0,-.61,0])
        animation=text('Animation: 3Blue1Brown’s ManimGL',21,MUTED).move_to([0,-1.4,0])
        repo=text('github.com/rezamirjaliliphd/ESPPRC',27,CYAN).move_to([0,-2.35,0])
        self.put(title,cspy,author,doi,sri,paper,doi2,animation,repo)
        self.play(FadeIn(title),FadeIn(cspy),FadeIn(author),FadeIn(doi),run_time=.7)
        self.play(FadeIn(sri),FadeIn(paper),FadeIn(doi2),FadeIn(animation),FadeIn(repo),run_time=.8)
        self.wait(6.5)
        self.snapshot('credits')

    def write_captions(self):
        def stamp(t):
            ms=round(t*1000)
            return f'{ms//3600000:02}:{ms//60000%60:02}:{ms//1000%60:02},{ms%1000:03}'
        blocks=[]
        for i,((start,caption),(end,_)) in enumerate(zip(self.captions,self.captions[1:]),1):
            blocks.append(f'{i}\n{stamp(start)} --> {stamp(end)}\n{caption}\n')
        (self.output_path/'espprc_showcase.srt').write_text('\n'.join(blocks),encoding='utf-8')
        normalize_subtitles(self.output_path/'espprc_showcase.srt')
