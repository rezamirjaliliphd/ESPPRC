"""Render with 3b1b/ManimGL, optionally using headless EGL on Linux."""
import argparse
import os
from pathlib import Path
import sys
import subprocess

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,default=Path(__file__).parent/'media')
    parser.add_argument('--preview',action='store_true',help='Render at 480 x 480, 15 fps')
    parser.add_argument('--headless',action='store_true',help='Use EGL instead of an X display')
    args=parser.parse_args()
    args.output=args.output.resolve()
    args.output.mkdir(parents=True,exist_ok=True)
    os.environ.setdefault('XDG_CACHE_HOME',str(args.output/'.cache'))
    os.environ.setdefault('MPLCONFIGDIR',str(args.output/'.cache/matplotlib'))
    os.environ.setdefault('OMP_NUM_THREADS','1')
    if args.headless:
        os.environ['PYGLET_HEADLESS']='true'
        import pyglet
        pyglet.options['headless']=True
        import moderngl
        original=moderngl.create_standalone_context
        def egl_context(**kwargs):
            kwargs.setdefault('backend','egl')
            return original(**kwargs)
        moderngl.create_standalone_context=egl_context
    # Manim derives projection constants at import time, so set the square
    # resolution through its CLI parser as well as the camera configuration.
    sys.argv=[sys.argv[0],'--resolution','480x480' if args.preview else '1080x1080']
    import imageio_ffmpeg
    from scene import ESPPRCShowcase, BG
    scene=ESPPRCShowcase(camera_config=dict(resolution=(480,480) if args.preview else (1080,1080),
                         fps=15 if args.preview else 30,background_color=BG),
                         file_writer_config=dict(write_to_movie=True,output_directory=str(args.output),
                         file_name='espprc_showcase',ffmpeg_bin=imageio_ffmpeg.get_ffmpeg_exe(),quiet=True))
    scene.frame.set_width(8,stretch=True)
    scene.frame.set_height(8,stretch=True)
    scene.output_path=args.output
    scene.run()
    # Move MP4 metadata to the start for immediate playback after uploading.
    movie=args.output/'espprc_showcase.mp4'
    temporary=args.output/'espprc_showcase_faststart.mp4'
    subprocess.run([imageio_ffmpeg.get_ffmpeg_exe(),'-v','error','-y','-i',str(movie),
                    '-c','copy','-movflags','+faststart',str(temporary)],check=True)
    temporary.replace(movie)
    print(f'Rendered {scene.time:.2f} seconds to {args.output}')

if __name__=='__main__':
    main()
