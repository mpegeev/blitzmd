# Zooming images

BlitzMD-only: GitHub has no way to zoom a picture inside the page. Images of at least 40×40 px zoom where they are, without moving the text around them:

- **Ctrl + mouse wheel** over an image zooms it around the mouse pointer (up to 10×). Anywhere else Ctrl + wheel zooms the whole page, as before. A touchpad pinch does the same.
- **Drag** a zoomed image to look around it; **double-click** it to go back.
- The **buttons** in its top right corner (shown while the mouse is over it, if it is big enough) zoom out, zoom in, go back, and open the **full-window view**.
- In the full-window view the wheel zooms without Ctrl, dragging moves the image, double-click fits it again, and **Esc**, **Backspace**, the × button or a click beside the image return to the page. `+`, `-`, `0` and the arrow keys work too.
- Right-click an image for *View image in the whole window* and, when zoomed, *Reset image zoom*.
- A click on an image that isn't zoomed still opens it in the system's image viewer (a linked image follows its link). Zoom isn't remembered: reloading the file shows everything as laid out. Printing and thumbnails always show images unzoomed.

## A photo

![A photo](img/photo.jpg)

## Vector graphics stay sharp at any zoom

![SVG with gradients and text](img/gradients.svg)

## A wide image, shrunk to fit the column

Zoom in to read the ruler; the full-window view shows it at its natural size.

![A wide image](img/wide-2400x300.png)

## An animated GIF keeps playing while zoomed

![Animated GIF](img/animated.gif)

## A linked image

A click follows the link; once zoomed, a click only drags.

[![The logo, linking to md4c](../logo.png)](https://github.com/mity/md4c)

## Images in a table

| Diagram | Transparent PNG |
|---|---|
| ![Diagram](../diagram.png) | ![Transparent PNG](img/transparent.png) |

## Too small to zoom

Badges and icons under 40 px ignore Ctrl + wheel (it zooms the page), so hovering them never gets in the way: ![build](img/badge-build-passing.svg) ![coverage](img/badge-coverage.svg) <img src="../avatar.png" width="24" height="24" alt="avatar">

## A tall image

![A tall image](img/tall-300x1200.png)
