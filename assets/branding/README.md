# Kindle 书库封面

- `kindle-cover.png`：内置 imagegen 生成的原图，1024×1536。
- `kindle-cover-thumbnail.png`：用于书库的 256×384 PNG，由原图等比缩放。
- 缩略图完整内嵌在 `kpm/scriptlets/xiangqi.sh` 的 `# Icon:` 中；专项测试校验两者一致。

生成提示词：

> Create one finished portrait book-cover image for a Kindle Chinese chess application. Aspect ratio 2:3, flat front-facing artwork, white background, pure black plus a few gray tones, designed for an e-ink home-screen thumbnail. The exact large Chinese title is 中国象棋, arranged horizontally near the top in bold elegant readable Chinese print. Beneath it, a striking simple illustration of two round traditional Chinese chess pieces on a small cropped xiangqi board: a light piece bearing exactly 帅 in front, and a dark piece bearing exactly 将 behind it. Broad confident ink strokes and ample empty space; instantly recognizable at tiny size. The light and dark pieces should remain legible in grayscale. No Western chess pieces, no phones or Kindle hardware, no mockup frame, no logos, no English text, no decorative border, no gradients, no fine textures. This is the cover art itself filling the image.
