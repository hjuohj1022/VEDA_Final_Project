# TeensyC

This firmware is a standard Zephyr application for `teensy41`.

The repository now includes a `west.yml` manifest, so it can be imported by MCUXpresso for VS Code as a Zephyr repository and used as a repository application.

## MCUXpresso Import

In MCUXpresso for VS Code:

```text
IMPORTED REPOSITORIES -> Import Local/Remote Repository -> LOCAL
```

Use these values:

- Repository type: `Zephyr`
- Repository path: this folder
- Manifest file: `west.yml`

After the repository is imported:

```text
Import Example / Create Project -> Choose repository -> Choose board teensy41
```

For the application type, choose:

- `Repository application`

That keeps the sources in this repository instead of copying them elsewhere.

## Workspace Setup

Create or choose a workspace parent directory, then initialize `west` around this app from that parent directory:

```powershell
west init -l TeensyC
west update
```

This creates a standard Zephyr workspace where this application is the manifest repository and Zephyr is fetched from the official upstream project at `v2.7.1`.

## Build

After the workspace is initialized, you can build from this directory with:

```powershell
west build -b teensy41 .
```

Or use the VS Code build task, which runs:

```powershell
west build -b teensy41 . -d build/teensy41-debug --pristine=auto
```

## Flash

```powershell
west flash -d build/teensy41-debug
```

The active Zephyr app entry points are:

- `CMakeLists.txt`
- `prj.conf`
- `app.overlay`
- `west.yml`
