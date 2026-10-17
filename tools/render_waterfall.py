"""Render real cs16 IQ as a 30-fps MP4, animated README GIF and still preview.

Usage: python tools/render_waterfall.py tools/cap/waterfall-final.cs16 docs/media
Requires numpy, matplotlib, Pillow, and imageio-ffmpeg (or local tmp/video-deps).
"""
import argparse
import json
from pathlib import Path
import subprocess
import sys
import numpy as np
from PIL import Image, ImageDraw, ImageFont
from matplotlib import colormaps

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tmp/video-deps'))
import imageio_ffmpeg


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('input', type=Path)
    ap.add_argument('output', type=Path)
    args = ap.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    fs, fps, nfft, hop = 20_000_000, 30, 2048, 20_000
    data = np.memmap(args.input, dtype='<i2', mode='r').reshape(-1, 2)
    duration = len(data)/fs
    rows = len(data)//hop
    w = np.hanning(nfft)
    psd = np.empty((rows, 1024), np.float32)
    for row in range(rows):
        a = data[row*hop:row*hop+9*nfft].astype(np.float32)/32768
        x = (a[:, 0]+1j*a[:, 1]).reshape(9, nfft)
        x -= x.mean(axis=1, keepdims=True)
        p = np.mean(abs(np.fft.fftshift(np.fft.fft(x*w, axis=1), axes=1))**2, axis=0)/(nfft*np.sum(w*w))
        psd[row] = 10*np.log10(p.reshape(1024, 2).mean(axis=1)+1e-20)
    low, high = float(np.percentile(psd, 12)), float(np.percentile(psd, 99.7))
    high = max(high, low+20)
    lut = (colormaps['magma'](np.linspace(0, 1, 256))[:, :3]*255).astype('uint8')
    pixels = lut[np.clip((psd-low)/(high-low)*255, 0, 255).astype('uint8')]
    font_path = 'C:/Windows/Fonts/segoeui.ttf'
    font = lambda size: ImageFont.truetype(font_path, size)
    x0, x1, y0, y1 = 100, 1180, 340, 650
    proc = subprocess.Popen([imageio_ffmpeg.get_ffmpeg_exe(), '-y', '-f', 'rawvideo', '-vcodec', 'rawvideo', '-pix_fmt', 'rgb24', '-s', '1280x720', '-r', str(fps), '-i', '-', '-an', '-c:v', 'libx264', '-crf', '19', '-pix_fmt', 'yuv420p', '-movflags', '+faststart', str(args.output/'waterfall.mp4')], stdin=subprocess.PIPE, stderr=subprocess.PIPE)
    frames=[]
    try:
        for frame in range(round(duration*fps)):
            t=(frame+1)/fps
            end=min(rows, round(t*1000))
            im=Image.new('RGB', (1280,720), '#080e1c')
            d=ImageDraw.Draw(im)
            d.text((54,28), 'RTL8822BU / AIRWAVES', font=font(30), fill='#e8f1ff')
            d.text((55,72), '2462 MHz   /   20 MSPS   /   BW20   /   REAL IQ CAPTURE', font=font(18), fill='#7f9ab8')
            d.rounded_rectangle((1010,30,1225,78), radius=12, fill='#132839')
            d.text((1030,39), f'REPLAY  {min(t,duration):04.2f} s', font=font(22), fill='#63e6cd')
            d.text((x0,112), 'SPECTRUM', font=font(15), fill='#7f9ab8')
            spec=np.mean(psd[max(0,end-33):end], axis=0)
            for db in range(-100, 1, 10):
                yy=round(290-(db-low)/(high-low)*145)
                if 135<=yy<=290:
                    d.line((x0,yy,x1,yy), fill='#1b2a3c')
                    d.text((42,yy-9), str(db), font=font(14), fill='#71859e')
            pts=[(x0+j*(x1-x0)/1023, float(np.clip(290-(v-low)/(high-low)*145,135,290))) for j,v in enumerate(spec)]
            d.polygon([(x0,298)]+pts+[(x1,298)], fill='#0e3348')
            d.line(pts, fill='#5ce1ef', width=2)
            d.text((x0,309), 'WATERFALL  /  newest at bottom  /  1 ms per row', font=font(15), fill='#7f9ab8')
            waterfall=np.zeros((310,1024,3),dtype='uint8')
            visible=pixels[max(0,end-310):end]
            waterfall[-len(visible):]=visible
            im.paste(Image.fromarray(waterfall).resize((x1-x0,310)), (x0,y0))
            for j in range(11):
                xx=x0+j*(x1-x0)/10
                d.line((xx,y1,xx,y1+5), fill='#7f9ab8')
                d.text((xx-21,y1+10), str(2452+j*2), font=font(15), fill='#adbed4')
            d.text((x1+12,y1+10),'MHz',font=font(14),fill='#7f9ab8')
            d.text((55,692), '2 seconds of received RF • fixed color scale • no synthetic signals',font=font(14),fill='#71859e')
            d.text((940,692),f'Color: {low:.0f} to {high:.0f} dBFS/bin',font=font(14),fill='#71859e')
            proc.stdin.write(im.tobytes())
            if frame%2==0:
                frames.append(im.resize((896,504),Image.Resampling.LANCZOS))
            if frame==round(duration*fps)//2:
                im.save(args.output/'waterfall.png')
    finally:
        proc.stdin.close()
    stderr=proc.stderr.read().decode(errors='replace')
    if proc.wait():
        raise RuntimeError(stderr)
    frames[0].save(args.output/'waterfall.gif',save_all=True,append_images=frames[1:],duration=67,loop=0,optimize=False)
    (args.output/'waterfall.json').write_text(json.dumps(dict(sample_rate=fs,center_hz=2462000000,samples=len(data),rf_seconds=duration,video_fps=fps,frames=round(duration*fps),fft_size=nfft,time_bin_ms=1,color_dbfs_bin=[low,high],source=str(args.input)),indent=2))
    print(f'Rendered {duration:.6f} seconds, {rows} waterfall rows, color {low:.1f}..{high:.1f} dBFS/bin')


if __name__=='__main__':
    main()
