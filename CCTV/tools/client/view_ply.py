import math
import sys
import tkinter as tk


def load_ply_ascii(path: str):
    with open(path, "r", encoding="utf-8", errors="ignore") as f:
        header = []
        for line in f:
            header.append(line.rstrip("\n"))
            if line.strip() == "end_header":
                break

        fmt = "ascii"
        vertex_count = 0
        properties = []
        in_vertex = False
        for line in header:
            parts = line.strip().split()
            if not parts:
                continue
            if parts[0] == "format":
                fmt = parts[1]
            elif parts[0] == "element" and len(parts) >= 3:
                in_vertex = parts[1] == "vertex"
                if in_vertex:
                    vertex_count = int(parts[2])
            elif parts[0] == "property" and in_vertex:
                properties.append(parts[-1])

        if fmt != "ascii":
            raise ValueError("Only ASCII PLY is supported by this viewer.")

        if vertex_count <= 0:
            raise ValueError("No vertex data found in PLY.")

        idx_x = properties.index("x") if "x" in properties else None
        idx_y = properties.index("y") if "y" in properties else None
        idx_z = properties.index("z") if "z" in properties else None
        if idx_x is None or idx_y is None or idx_z is None:
            raise ValueError("PLY must include x/y/z properties.")

        points = []
        for _ in range(vertex_count):
            line = f.readline()
            if not line:
                break
            vals = line.strip().split()
            if len(vals) <= max(idx_x, idx_y, idx_z):
                continue
            x = float(vals[idx_x])
            y = float(vals[idx_y])
            z = float(vals[idx_z])
            points.append((x, y, z))

    return points


def rotate_point(x, y, z, rx, ry):
    # Rotate around Y
    cos_y = math.cos(ry)
    sin_y = math.sin(ry)
    x1 = x * cos_y + z * sin_y
    z1 = -x * sin_y + z * cos_y

    # Rotate around X
    cos_x = math.cos(rx)
    sin_x = math.sin(rx)
    y1 = y * cos_x - z1 * sin_x
    z2 = y * sin_x + z1 * cos_x

    return x1, y1, z2


def normalize_points(points, max_points=200_000):
    if len(points) > max_points:
        step = max(1, len(points) // max_points)
        points = points[::step]

    xs = [p[0] for p in points]
    ys = [p[1] for p in points]
    zs = [p[2] for p in points]
    min_x, max_x = min(xs), max(xs)
    min_y, max_y = min(ys), max(ys)
    min_z, max_z = min(zs), max(zs)
    return points, (min_x, max_x, min_y, max_y, min_z, max_z)


def color_from_depth(z, min_z, max_z):
    if max_z - min_z < 1e-6:
        t = 0.5
    else:
        t = (z - min_z) / (max_z - min_z)
    # blue -> cyan -> green -> yellow -> red
    r = int(255 * min(1.0, max(0.0, 2.0 * (t - 0.5))))
    g = int(255 * min(1.0, max(0.0, 2.0 * (0.5 - abs(t - 0.5)))))
    b = int(255 * min(1.0, max(0.0, 1.0 - 2.0 * t)))
    return f"#{r:02x}{g:02x}{b:02x}"


def main() -> int:
    if len(sys.argv) < 2:
        print("Usage: python view_ply.py <path_to_ply>")
        return 2

    ply_path = sys.argv[1]
    try:
        points = load_ply_ascii(ply_path)
    except Exception as e:
        print(f"Failed to load PLY: {e}")
        return 1

    if not points:
        print("Point cloud is empty.")
        return 1

    points, bounds = normalize_points(points)
    min_x, max_x, min_y, max_y, min_z, max_z = bounds

    width, height = 1000, 700
    padding = 20

    rx = math.radians(-20.0)
    ry = math.radians(35.0)

    rotated = [rotate_point(x, y, z, rx, ry) for x, y, z in points]
    xs = [p[0] for p in rotated]
    ys = [p[1] for p in rotated]
    zs = [p[2] for p in rotated]

    min_rx, max_rx = min(xs), max(xs)
    min_ry, max_ry = min(ys), max(ys)
    min_rz, max_rz = min(zs), max(zs)

    scale_x = (width - 2 * padding) / (max_rx - min_rx + 1e-9)
    scale_y = (height - 2 * padding) / (max_ry - min_ry + 1e-9)
    scale = min(scale_x, scale_y)

    root = tk.Tk()
    root.title(f"PLY Viewer (points={len(points)})")
    canvas = tk.Canvas(root, width=width, height=height, bg="black")
    canvas.pack()

    for (x, y, z) in rotated:
        px = (x - min_rx) * scale + padding
        py = height - ((y - min_ry) * scale + padding)
        color = color_from_depth(z, min_rz, max_rz)
        canvas.create_rectangle(px, py, px + 1, py + 1, outline=color, fill=color)

    root.mainloop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
