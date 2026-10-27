# grab the screen into shots/<name>.png at half size
import sys
from PIL import ImageGrab
im = ImageGrab.grab(all_screens=True)
im.resize((im.size[0] // 2, im.size[1] // 2)).save('D:/Projekte/splitsecond_pad/shots/%s.png' % sys.argv[1])
print(im.size)
