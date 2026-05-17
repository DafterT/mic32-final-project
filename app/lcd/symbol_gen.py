import tkinter as tk
from tkinter import filedialog

WIDTH = 10
HEIGHT = 16
CELL_SIZE = 32

pixels = [[0 for _ in range(WIDTH)] for _ in range(HEIGHT)]


def toggle_pixel(event):
    x = event.x // CELL_SIZE
    y = event.y // CELL_SIZE

    if 0 <= x < WIDTH and 0 <= y < HEIGHT:
        pixels[y][x] ^= 1
        draw_grid()
        export_arrays()


def draw_grid():
    canvas.delete("all")

    for y in range(HEIGHT):
        for x in range(WIDTH):
            color = "black" if pixels[y][x] else "white"

            canvas.create_rectangle(
                x * CELL_SIZE,
                y * CELL_SIZE,
                (x + 1) * CELL_SIZE,
                (y + 1) * CELL_SIZE,
                fill=color,
                outline="gray"
            )


def get_char_bytes(x_offset, y_offset):
    result = []

    for y in range(8):
        byte = 0

        for x in range(5):
            if pixels[y_offset + y][x_offset + x]:
                bit = 4 - x
                byte |= 1 << bit

        result.append(byte)

    return result


def generate_c_code():
    chars = [
        ("LCD_BITMAP_0_TOP_LEFT", 0, 0),
        ("LCD_BITMAP_1_TOP_RIGHT", 5, 0),
        ("LCD_BITMAP_2_BOTTOM_LEFT", 0, 8),
        ("LCD_BITMAP_3_BOTTOM_RIGHT", 5, 8),
    ]

    output = []

    for name, x_offset, y_offset in chars:
        data = get_char_bytes(x_offset, y_offset)

        output.append(f"static const uint8_t {name}[8] = {{")
        for byte in data:
            output.append(f"    0x{byte:02X},")
        output.append("};")
        output.append("")

    return "\n".join(output)


def export_arrays():
    code = generate_c_code()

    text.config(state=tk.NORMAL)
    text.delete("1.0", tk.END)
    text.insert(tk.END, code)
    text.config(state=tk.NORMAL)


def copy_to_clipboard():
    code = generate_c_code()

    root.clipboard_clear()
    root.clipboard_append(code)
    root.update()

    status_label.config(text="Скопировано в буфер обмена")


def save_to_file():
    code = generate_c_code()

    file_path = filedialog.asksaveasfilename(
        defaultextension=".h",
        filetypes=[
            ("Header file", "*.h"),
            ("C file", "*.c"),
            ("Text file", "*.txt"),
            ("All files", "*.*"),
        ]
    )

    if file_path:
        with open(file_path, "w", encoding="utf-8") as file:
            file.write(code)

        status_label.config(text=f"Сохранено: {file_path}")


def clear_canvas():
    for y in range(HEIGHT):
        for x in range(WIDTH):
            pixels[y][x] = 0

    draw_grid()
    export_arrays()
    status_label.config(text="Очищено")


root = tk.Tk()
root.title("10x16 LCD Bitmap Editor")

canvas = tk.Canvas(
    root,
    width=WIDTH * CELL_SIZE,
    height=HEIGHT * CELL_SIZE,
    bg="white"
)
canvas.pack(side=tk.LEFT, padx=10, pady=10)

canvas.bind("<Button-1>", toggle_pixel)

right_frame = tk.Frame(root)
right_frame.pack(side=tk.RIGHT, fill=tk.BOTH, expand=True, padx=10, pady=10)

copy_button = tk.Button(
    right_frame,
    text="Copy to clipboard",
    command=copy_to_clipboard
)
copy_button.pack(fill=tk.X)

save_button = tk.Button(
    right_frame,
    text="Save to .h",
    command=save_to_file
)
save_button.pack(fill=tk.X, pady=5)

clear_button = tk.Button(
    right_frame,
    text="Clear",
    command=clear_canvas
)
clear_button.pack(fill=tk.X)

status_label = tk.Label(
    right_frame,
    text="",
    anchor="w"
)
status_label.pack(fill=tk.X, pady=5)

text = tk.Text(
    right_frame,
    width=50,
    height=35,
    wrap=tk.NONE
)
text.pack(fill=tk.BOTH, expand=True)

draw_grid()
export_arrays()

root.mainloop()