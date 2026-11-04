"""Package the approved logo into Apple asset catalogs. Requires Pillow."""
from pathlib import Path
import json
from PIL import Image

root = Path(__file__).resolve().parents[2]
catalog = root / 'App/Resources/Assets.xcassets'
info = {'author': 'xcode', 'version': 1}
def contents(folder, images=None):
    folder.mkdir(parents=True, exist_ok=True)
    data = {'info': info}
    if images is not None:
        data['images'] = images
    (folder / 'Contents.json').write_text(json.dumps(data, indent=2) + '\n')

logo = Image.open(root / 'Design/Brand/sculpt3d-logo.png').convert('RGBA')
# The approved horizontal lockup places the standalone symbol in its left section.
mark = logo.crop((160, 100, 640, 640))
mark = mark.crop(mark.getbbox())
contents(catalog)
for name, image in [('BrandLogo', logo.crop(logo.getbbox())), ('BrandMark', mark)]:
    folder = catalog / f'{name}.imageset'
    contents(folder, [{'filename': f'{name}.png', 'idiom': 'universal'}])
    image.save(folder / f'{name}.png')

# App icons must be opaque; retain the approved symbol on a white field.
icon = Image.new('RGB', (1024, 1024), 'white')
scaled = mark.copy()
scaled.thumbnail((740, 740), Image.Resampling.LANCZOS)
icon.paste(scaled, ((1024-scaled.width)//2, (1024-scaled.height)//2), scaled)
folder = catalog / 'AppIcon.appiconset'
images = [{'filename': 'icon-1024.png', 'idiom': 'universal', 'platform': 'ios', 'size': '1024x1024'}]
contents(folder)
icon.save(folder / 'icon-1024.png')
for size in [16, 32, 128, 256, 512]:
    for scale in [1, 2]:
        pixels = size * scale
        filename = f'icon-{pixels}.png'
        icon.resize((pixels, pixels), Image.Resampling.LANCZOS).save(folder / filename)
        images.append({'filename': filename, 'idiom': 'mac', 'size': f'{size}x{size}', 'scale': f'{scale}x'})
contents(folder, images)
