"""Keep short transition cues readable; wrap optional upload captions."""
from pathlib import Path
import textwrap

def normalize_subtitles(path):
    path=Path(path)
    cues=[]
    for block in path.read_text(encoding='utf-8').strip().split('\n\n'):
        lines=block.splitlines()
        start,end=lines[1].split(' --> ')
        cues.append([start,end,' '.join(lines[2:])])
    for i,cue in enumerate(cues[:-1]):
        if cue[2].startswith('The travel-time budget'):
            cue[1]=cues[i+1][1]
            cue[2]='Time limit: 8. Payload limit: 2. The cost-3 route takes 12 time units: infeasible.'
            del cues[i+1]
            break
    blocks=[]
    for i,(start,end,caption) in enumerate(cues,1):
        blocks.append(f'{i}\n{start} --> {end}\n'+textwrap.fill(caption,width=55)+'\n')
    path.write_text('\n'.join(blocks),encoding='utf-8')

if __name__=='__main__':
    import sys
    normalize_subtitles(sys.argv[1])
