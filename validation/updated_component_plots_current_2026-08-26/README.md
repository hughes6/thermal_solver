# Updated component plots

These files were rendered from the preserved `geometry_input.txt`.
All component air regions use `tab:cyan` in both fill and edge color.
`PLOT_MANIFEST.json` records the input, plotter and output hashes,
tool versions, source revision, and exact commands.

The 14-image batch completed normally. The full artist-level plot regression
also passed 10/10, including identical fill and edge colors for multiple air
regions and matching rack/component air colors. The isolated Matplotlib
runtime used for rendering was removed afterward (3,377 redundant dependency
files, 123,993,592 bytes); re-render from the project root with any Python
environment containing Matplotlib, NumPy, and pandas:

```powershell
python tools/render_component_plots.py "C:\Users\hconn\Downloads\Thermal Sim\v2.2\validation\updated_component_plots_current_2026-08-26\geometry_input.txt" --output-dir "C:\Users\hconn\Downloads\Thermal Sim\v2.2\validation\updated_component_plots_current_2026-08-26"
```

Component count: 13
