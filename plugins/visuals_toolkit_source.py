
from pydantic import BaseModel, Field

try:
    from fastapi.responses import HTMLResponse
except Exception:
    HTMLResponse = None  # type: ignore


ChartType = Literal["bar", "line", "scatter"]
OutputMode = Literal["embed", "text", "auto"]


class Tools:
    """
    Visuals Toolkit: make tables, charts, and ASCII diagrams.
    Works in pure text mode (no external dependencies) or enhanced HTML mode.
    """

    def __init__(self):
        self.valves = self.Valves()

    class Valves(BaseModel):
        allow_external_cdn: bool = Field(
            True,
            description="If true, charts can load Plotly from CDN for interactive visuals. If false, uses ASCII charts.",
        )
        max_rows: int = Field(
            200,
            description="Safety cap for table rows rendered to avoid massive outputs.",
        )
        max_cols: int = Field(
            50,
            description="Safety cap for table columns rendered to avoid massive outputs.",
        )
        chart_height: int = Field(
            12,
            description="Height of ASCII charts in lines (text mode)",
        )
        chart_width: int = Field(
            60,
            description="Width of ASCII charts in characters (text mode)",
        )

    async def render_table(
        self,
        rows: List[Dict[str, Any]],
        *,
        title: str = "Table",
        mode: OutputMode = "auto",
    ) -> Any:
        """
        Render a table from a list of row objects (each row is a dict).
        - mode="embed": returns HTMLResponse (iframe embed in chat)
        - mode="text": returns a Markdown table string
        - mode="auto": automatically chooses best mode

        :param rows: List of dict rows (keys become columns)
        :param title: Title shown above the table
        :param mode: "embed", "text", or "auto"
        """
        if not rows:
            return "No rows provided."

        mode = self._resolve_mode(mode)

        # Determine columns as union of keys in order of first appearance
        cols: List[str] = []
        seen = set()
        for r in rows:
            for k in r.keys():
                if k not in seen:
                    seen.add(k)
                    cols.append(str(k))

        cols = cols[: self.valves.max_cols]
        rows = rows[: self.valves.max_rows]

        if mode == "text":
            return self._markdown_table(title, cols, rows)

        if HTMLResponse is None:
            return self._markdown_table(title, cols, rows)

        html_table = self._html_table(title, cols, rows)
        page = self._wrap_html(title=title, body=html_table)
        return HTMLResponse(content=page, headers={"Content-Disposition": "inline"})

    async def render_comparison_table(
        self,
        items: List[str],
        criteria: List[str],
        scores: Dict[str, Dict[str, Any]],
        *,
        title: str = "Comparison",
        highlight_best: bool = True,
        mode: OutputMode = "auto",
    ) -> Any:
        """
        Compare items across criteria with optional highlighting.

        :param items: List of items being compared
        :param criteria: List of criteria to compare on
        :param scores: Dict mapping item names to their criterion values
        :param title: Table title
        :param highlight_best: If True, marks best values with ★
        :param mode: "embed", "text", or "auto"

        Example:
        items = ["Option A", "Option B"]
        criteria = ["Cost", "Speed"]
        scores = {"Option A": {"Cost": 10, "Speed": 95}}
        """
        mode = self._resolve_mode(mode)

        # Build rows
        rows = []
        for item in items:
            row = {"Item": item}
            item_scores = scores.get(item, {})
            for criterion in criteria:
                row[criterion] = item_scores.get(criterion, "—")
            rows.append(row)

        if mode == "text":
            # Find best values for each numeric criterion
            if highlight_best:
                best_vals = self._find_best_values(criteria, scores, items)
                # Add ★ marker to best values
                for row in rows:
                    for criterion in criteria:
                        val = row.get(criterion)
                        if criterion in best_vals and val in best_vals[criterion]:
                            row[criterion] = f"{val} ★"

            cols = ["Item"] + criteria
            return self._markdown_table(title, cols, rows)

        if HTMLResponse is None:
            cols = ["Item"] + criteria
            return self._markdown_table(title, cols, rows)

        # HTML version with highlighting
        best_vals = (
            self._find_best_values(criteria, scores, items) if highlight_best else {}
        )
        cols = ["Item"] + criteria
        html_table = self._html_comparison_table(title, cols, rows, best_vals)
        page = self._wrap_html(title=title, body=html_table)
        return HTMLResponse(content=page, headers={"Content-Disposition": "inline"})

    async def render_chart(
        self,
        x: List[Any],
        y: List[float],
        *,
        title: str = "Chart",
        chart_type: ChartType = "line",
        mode: OutputMode = "auto",
        x_label: str = "",
        y_label: str = "",
    ) -> Any:
        """
        Render a chart (ASCII in text mode, Plotly in embed mode).

        :param x: X values (labels)
        :param y: Y values (numbers)
        :param title: Chart title
        :param chart_type: "bar" | "line" | "scatter"
        :param mode: "embed", "text", or "auto"
        :param x_label: optional x-axis label
        :param y_label: optional y-axis label
        """
        if len(x) != len(y):
            return "x and y must have the same length."

        mode = self._resolve_mode(mode)

        if mode == "text":
            return self._ascii_chart(x, y, title, chart_type, x_label, y_label)

        if not self.valves.allow_external_cdn or HTMLResponse is None:
            return self._ascii_chart(x, y, title, chart_type, x_label, y_label)

        # Plotly embed
        safe_title = html.escape(title)
        chart_id = "chart"
        payload = {
            "x": x,
            "y": y,
            "type": "bar" if chart_type == "bar" else "scatter",
            "mode": "lines+markers" if chart_type == "line" else "markers",
        }
        if chart_type == "scatter":
            payload["mode"] = "markers"
        if chart_type == "bar":
            payload.pop("mode", None)

        layout = {
            "title": {"text": safe_title},
            "margin": {"l": 50, "r": 20, "t": 50, "b": 50},
        }
        if x_label:
            layout["xaxis"] = {"title": {"text": html.escape(x_label)}}
        if y_label:
            layout["yaxis"] = {"title": {"text": html.escape(y_label)}}

        body = f"""
<div style="font-family: system-ui, -apple-system, Segoe UI, Roboto, sans-serif;">
  <div id="{chart_id}" style="width:100%;height:420px;"></div>
</div>
<script src="https://cdn.plot.ly/plotly-latest.min.js"></script>
<script>
  const data = [{json.dumps(payload)}];
  const layout = {json.dumps(layout)};
  Plotly.newPlot("{chart_id}", data, layout, {{responsive: true}});
</script>
"""
        page = self._wrap_html(title=title, body=body)
        return HTMLResponse(content=page, headers={"Content-Disposition": "inline"})

    async def render_multi_chart(
        self,
        series: List[Dict[str, Any]],
        *,
        title: str = "Chart",
        chart_type: ChartType = "line",
        mode: OutputMode = "auto",
        x_label: str = "",
        y_label: str = "",
    ) -> Any:
        """
        Render a chart with multiple series.

        :param series: List of series dicts, each with "name", "x", and "y" keys
        :param title: Chart title
        :param chart_type: "bar" | "line" | "scatter"
        :param mode: "embed", "text", or "auto"

        Example:
        series = [
            {"name": "Sales", "x": ["Jan", "Feb"], "y": [100, 150]},
            {"name": "Costs", "x": ["Jan", "Feb"], "y": [80, 90]},
        ]
        """
        if not series:
            return "No series provided."

        mode = self._resolve_mode(mode)

        if mode == "text":
            # Render each series as separate ASCII chart
            output = f"### {title}\n\n"
            for s in series:
                name = s.get("name", "Series")
                x = s.get("x", [])
                y = s.get("y", [])
                output += self._ascii_chart(x, y, name, chart_type, x_label, y_label)
                output += "\n\n"
            return output

        if not self.valves.allow_external_cdn or HTMLResponse is None:
            output = f"### {title}\n\n"
            for s in series:
                name = s.get("name", "Series")
                x = s.get("x", [])
                y = s.get("y", [])
                output += self._ascii_chart(x, y, name, chart_type, x_label, y_label)
                output += "\n\n"
            return output

        # Plotly multi-series
        safe_title = html.escape(title)
        chart_id = "multichart"

        traces = []
        for s in series:
            name = s.get("name", "Series")
            x = s.get("x", [])
            y = s.get("y", [])

            trace = {
                "name": name,
                "x": x,
                "y": y,
                "type": "bar" if chart_type == "bar" else "scatter",
            }

            if chart_type == "line":
                trace["mode"] = "lines+markers"
            elif chart_type == "scatter":
                trace["mode"] = "markers"
            elif chart_type == "bar":
                trace.pop("mode", None)

            traces.append(trace)

        layout = {
            "title": {"text": safe_title},
            "margin": {"l": 50, "r": 20, "t": 50, "b": 50},
        }
        if x_label:
            layout["xaxis"] = {"title": {"text": html.escape(x_label)}}
        if y_label:
            layout["yaxis"] = {"title": {"text": html.escape(y_label)}}

        body = f"""
<div style="font-family: system-ui, -apple-system, Segoe UI, Roboto, sans-serif;">
  <div id="{chart_id}" style="width:100%;height:420px;"></div>
</div>
<script src="https://cdn.plot.ly/plotly-latest.min.js"></script>
<script>
  const data = {json.dumps(traces)};
  const layout = {json.dumps(layout)};
  Plotly.newPlot("{chart_id}", data, layout, {{responsive: true}});
</script>
"""
        page = self._wrap_html(title=title, body=body)
        return HTMLResponse(content=page, headers={"Content-Disposition": "inline"})

    async def render_heatmap(
        self,
        data: List[List[float]],
        row_labels: List[str],
        col_labels: List[str],
        *,
        title: str = "Heatmap",
        mode: OutputMode = "auto",
    ) -> Any:
        """
        Render a heatmap (ASCII blocks in text mode, Plotly in embed).

        :param data: 2D list of numeric values (rows x cols)
        :param row_labels: Labels for rows
        :param col_labels: Labels for columns
        :param title: Heatmap title
        :param mode: "embed", "text", or "auto"
        """
        mode = self._resolve_mode(mode)

        if mode == "text":
            return self._ascii_heatmap(data, row_labels, col_labels, title)

        if not self.valves.allow_external_cdn or HTMLResponse is None:
            return self._ascii_heatmap(data, row_labels, col_labels, title)

        # Plotly heatmap
        safe_title = html.escape(title)
        chart_id = "heatmap"

        trace = {
            "type": "heatmap",
            "z": data,
            "x": col_labels,
            "y": row_labels,
            "colorscale": "Viridis",
        }

        layout = {
            "title": {"text": safe_title},
            "margin": {"l": 100, "r": 20, "t": 50, "b": 100},
        }

        body = f"""
<div style="font-family: system-ui, -apple-system, Segoe UI, Roboto, sans-serif;">
  <div id="{chart_id}" style="width:100%;height:500px;"></div>
</div>
<script src="https://cdn.plot.ly/plotly-latest.min.js"></script>
<script>
  const data = [{json.dumps(trace)}];
  const layout = {json.dumps(layout)};
  Plotly.newPlot("{chart_id}", data, layout, {{responsive: true}});
</script>
"""
        page = self._wrap_html(title=title, body=body)
        return HTMLResponse(content=page, headers={"Content-Disposition": "inline"})

    async def render_metrics_grid(
        self,
        metrics: Dict[str, Any],
        *,
        title: str = "Metrics",
        columns: int = 3,
        mode: OutputMode = "auto",
    ) -> Any:
        """
        Render key metrics in a grid layout.

        :param metrics: Dict of metric name to value
        :param title: Grid title
        :param columns: Number of columns in grid
        :param mode: "embed", "text", or "auto"

        Example:
        metrics = {"Revenue": "$1.2M", "Users": 15420, "Growth": "+23%"}
        """
        mode = self._resolve_mode(mode)

        if mode == "text":
            output = f"### {title}\n\n"
            for name, value in metrics.items():
                output += f"**{name}:** {value}  \n"
            return output

        if HTMLResponse is None:
            output = f"### {title}\n\n"
            for name, value in metrics.items():
                output += f"**{name}:** {value}  \n"
            return output

        # HTML grid
        cards = []
        for name, value in metrics.items():
            cards.append(
                f"""
<div style="background:#f8f9fa;border-radius:8px;padding:20px;text-align:center;border:1px solid #e9ecef;">
  <div style="font-size:14px;color:#6c757d;margin-bottom:8px;">{html.escape(str(name))}</div>
  <div style="font-size:28px;font-weight:600;color:#212529;">{html.escape(str(value))}</div>
</div>
"""
            )

        grid_style = (
            f"display:grid;grid-template-columns:repeat({columns},1fr);gap:16px;"
        )
        body = f"""
<div style="font-family: system-ui, -apple-system, Segoe UI, Roboto, sans-serif;">
  <h3 style="margin:0 0 16px 0;">{html.escape(title)}</h3>
  <div style="{grid_style}">
    {''.join(cards)}
  </div>
</div>
"""
        page = self._wrap_html(title=title, body=body)
        return HTMLResponse(content=page, headers={"Content-Disposition": "inline"})

    async def render_timeline(
        self,
        events: List[Dict[str, Any]],
        *,
        title: str = "Timeline",
        mode: OutputMode = "auto",
    ) -> Any:
        """
        Render a timeline of events.

        :param events: List of event dicts with "date", "event", and optional "details" keys
        :param title: Timeline title
        :param mode: "embed", "text", or "auto"

        Example:
        events = [
            {"date": "2024-01", "event": "Launch", "details": "Initial release"},
        ]
        """
        mode = self._resolve_mode(mode)

        if mode == "text":
            output = f"### {title}\n\n```\n"
            for event in events:
                date = event.get("date", "")
                evt = event.get("event", "")
                details = event.get("details", "")
                output += f"{date:12} │ {evt}\n"
                if details:
                    output += f"{'':12} │ {details}\n"
                output += "\n"
            output += "```"
            return output

        if HTMLResponse is None:
            output = f"### {title}\n\n```\n"
            for event in events:
                date = event.get("date", "")
                evt = event.get("event", "")
                details = event.get("details", "")
                output += f"{date:12} │ {evt}\n"
                if details:
                    output += f"{'':12} │ {details}\n"
                output += "\n"
            output += "```"
            return output

        # HTML timeline
        items = []
        for event in events:
            date = html.escape(str(event.get("date", "")))
            evt = html.escape(str(event.get("event", "")))
            details = html.escape(str(event.get("details", "")))

            items.append(
                f"""
<div style="display:flex;gap:16px;margin-bottom:24px;">
  <div style="min-width:100px;font-weight:600;color:#495057;">{date}</div>
  <div style="flex:1;border-left:2px solid #dee2e6;padding-left:16px;">
    <div style="font-weight:600;margin-bottom:4px;">{evt}</div>
    {f'<div style="color:#6c757d;font-size:14px;">{details}</div>' if details else ''}
  </div>
</div>
"""
            )

        body = f"""
<div style="font-family: system-ui, -apple-system, Segoe UI, Roboto, sans-serif;">
  <h3 style="margin:0 0 24px 0;">{html.escape(title)}</h3>
  <div>
    {''.join(items)}
  </div>
</div>
"""
        page = self._wrap_html(title=title, body=body)
        return HTMLResponse(content=page, headers={"Content-Disposition": "inline"})

    async def render_dashboard(
        self,
        components: List[Dict[str, Any]],
        *,
        title: str = "Dashboard",
        layout: Literal["grid", "vertical"] = "vertical",
        mode: OutputMode = "auto",
    ) -> Any:
        """
        Combine multiple visualizations into one dashboard.

        :param components: List of component dicts with "type" and "data" keys
        :param title: Dashboard title
        :param layout: "grid" or "vertical" (text mode always uses vertical)
        :param mode: "embed", "text", or "auto"

        Example:
        components = [
            {"type": "metrics", "data": {"metrics": {"Users": 1000}, "title": "KPIs"}},
            {"type": "chart", "data": {"x": [1,2,3], "y": [10,20,15], "title": "Growth"}},
        ]
        """
        mode = self._resolve_mode(mode)

        if mode == "text":
            # Text dashboard: concatenate components vertically
            output = f"# {title}\n\n"
            output += "═" * 60 + "\n\n"

            for i, comp in enumerate(components):
                comp_type = comp.get("type", "")
                comp_data = comp.get("data", {})

                if comp_type == "metrics":
                    metrics = comp_data.get("metrics", {})
                    comp_title = comp_data.get("title", f"Metrics {i+1}")
                    output += f"## {comp_title}\n\n"
                    for name, value in metrics.items():
                        output += f"**{name}:** {value}  \n"
                    output += "\n"

                elif comp_type == "chart":
                    x = comp_data.get("x", [])
                    y = comp_data.get("y", [])
                    comp_title = comp_data.get("title", f"Chart {i+1}")
                    chart_type = comp_data.get("chart_type", "line")
                    output += self._ascii_chart(x, y, comp_title, chart_type, "", "")
                    output += "\n\n"

                elif comp_type == "table":
                    rows = comp_data.get("rows", [])
                    comp_title = comp_data.get("title", f"Table {i+1}")
                    if rows:
                        cols = list(rows[0].keys())
                        output += self._markdown_table(comp_title, cols, rows)
                        output += "\n\n"

                output += "─" * 60 + "\n\n"

            return output

        if not self.valves.allow_external_cdn or HTMLResponse is None:
            # Fallback to text
            output = f"# {title}\n\n"
            output += "═" * 60 + "\n\n"

            for i, comp in enumerate(components):
                comp_type = comp.get("type", "")
                comp_data = comp.get("data", {})

                if comp_type == "metrics":
                    metrics = comp_data.get("metrics", {})
                    comp_title = comp_data.get("title", f"Metrics {i+1}")
                    output += f"## {comp_title}\n\n"
                    for name, value in metrics.items():
                        output += f"**{name}:** {value}  \n"
                    output += "\n"

                elif comp_type == "chart":
                    x = comp_data.get("x", [])
                    y = comp_data.get("y", [])
                    comp_title = comp_data.get("title", f"Chart {i+1}")
                    chart_type = comp_data.get("chart_type", "line")
                    output += self._ascii_chart(x, y, comp_title, chart_type, "", "")
                    output += "\n\n"

                elif comp_type == "table":
                    rows = comp_data.get("rows", [])
                    comp_title = comp_data.get("title", f"Table {i+1}")
                    if rows:
                        cols = list(rows[0].keys())
                        output += self._markdown_table(comp_title, cols, rows)
                        output += "\n\n"

                output += "─" * 60 + "\n\n"

            return output

        # HTML dashboard
        sections = []

        for i, comp in enumerate(components):
            comp_type = comp.get("type", "")
            comp_data = comp.get("data", {})

            if comp_type == "metrics":
                metrics = comp_data.get("metrics", {})
                comp_title = comp_data.get("title", f"Metrics {i+1}")
                columns = comp_data.get("columns", 3)
                cards = []
                for name, value in metrics.items():
                    cards.append(
                        f"""
<div style="background:#f8f9fa;border-radius:8px;padding:16px;text-align:center;border:1px solid #e9ecef;">
  <div style="font-size:12px;color:#6c757d;margin-bottom:6px;">{html.escape(str(name))}</div>
  <div style="font-size:24px;font-weight:600;color:#212529;">{html.escape(str(value))}</div>
</div>
"""
                    )
                grid_style = f"display:grid;grid-template-columns:repeat({columns},1fr);gap:12px;"
                sections.append(
                    f"""
<div style="margin-bottom:24px;">
  <h4 style="margin:0 0 12px 0;">{html.escape(comp_title)}</h4>
  <div style="{grid_style}">{''.join(cards)}</div>
</div>
"""
                )

            elif comp_type == "chart":
                x = comp_data.get("x", [])
                y = comp_data.get("y", [])
                comp_title = comp_data.get("title", f"Chart {i+1}")
                chart_type = comp_data.get("chart_type", "line")
                chart_id = f"chart_{i}"

                payload = {
                    "x": x,
                    "y": y,
                    "type": "bar" if chart_type == "bar" else "scatter",
                    "mode": "lines+markers" if chart_type == "line" else "markers",
                }
                if chart_type == "bar":
                    payload.pop("mode", None)

                layout_config = {
                    "title": {"text": html.escape(comp_title), "font": {"size": 16}},
                    "margin": {"l": 50, "r": 20, "t": 40, "b": 40},
                }

                sections.append(
                    f"""
<div style="margin-bottom:24px;border:1px solid #e9ecef;border-radius:8px;padding:12px;">
  <div id="{chart_id}" style="width:100%;height:300px;"></div>
  <script>
    Plotly.newPlot("{chart_id}", [{json.dumps(payload)}], {json.dumps(layout_config)}, {{responsive: true}});
  </script>
</div>
"""
                )

            elif comp_type == "table":
                rows = comp_data.get("rows", [])
                comp_title = comp_data.get("title", f"Table {i+1}")
                if rows:
                    cols = list(rows[0].keys())
                    table_html = self._html_table_simple(cols, rows)
                    sections.append(
                        f"""
<div style="margin-bottom:24px;">
  <h4 style="margin:0 0 12px 0;">{html.escape(comp_title)}</h4>
  {table_html}
</div>
"""
                    )

        layout_style = (
            "display:grid;grid-template-columns:1fr;gap:16px;"
            if layout == "vertical"
            else "display:grid;grid-template-columns:repeat(auto-fit,minmax(400px,1fr));gap:16px;"
        )

        body = f"""
<div style="font-family: system-ui, -apple-system, Segoe UI, Roboto, sans-serif;">
  <h2 style="margin:0 0 24px 0;">{html.escape(title)}</h2>
  <div style="{layout_style}">
    {''.join(sections)}
  </div>
</div>
<script src="https://cdn.plot.ly/plotly-latest.min.js"></script>
"""
        page = self._wrap_html(title=title, body=body)
        return HTMLResponse(content=page, headers={"Content-Disposition": "inline"})

    async def render_ascii_diagram(
        self,
        diagram: str,
        *,
        title: str = "Diagram",
    ) -> str:
        """
        Return a monospace ASCII/Unicode diagram as plain text.

        :param diagram: The diagram content using box-drawing characters
        :param title: Title to show above the diagram
        """
        return f"### {title}\n\n```text\n{diagram.rstrip()}\n```"

    async def render_flowchart(
        self,
        steps: List[str],
        *,
        title: str = "Flowchart",
    ) -> str:
        """
        Auto-generate simple ASCII flowchart from steps.

        :param steps: List of step descriptions
        :param title: Flowchart title
        """
        lines = []
        for i, step in enumerate(steps):
            box_width = min(max(len(step) + 4, 20), 60)
            lines.append("┌" + "─" * (box_width - 2) + "┐")
            lines.append("│ " + step.ljust(box_width - 4) + " │")
            lines.append("└" + "─" * (box_width - 2) + "┘")

            if i < len(steps) - 1:
                lines.append(" " * ((box_width - 1) // 2) + "↓")

        diagram = "\n".join(lines)
        return f"### {title}\n\n```text\n{diagram}\n```"

    async def render_tree(
        self,
        data: Dict[str, Any],
        *,
        title: str = "Tree",
        root_name: str = "Root",
    ) -> str:
        """
        Render hierarchical data as ASCII tree.

        :param data: Nested dict representing tree structure
        :param title: Tree title
        :param root_name: Name of the root node
        """

        def build_tree(
            node: Dict[str, Any], prefix: str = "", is_last: bool = True
        ) -> List[str]:
            lines = []
            items = list(node.items())
            for i, (key, value) in enumerate(items):
                is_last_item = i == len(items) - 1
                connector = "└── " if is_last_item else "├── "
                lines.append(prefix + connector + str(key))

                if isinstance(value, dict) and value:
                    extension = "    " if is_last_item else "│   "
                    lines.extend(build_tree(value, prefix + extension, is_last_item))
            return lines

        tree_lines = [root_name]
        tree_lines.extend(build_tree(data))
        diagram = "\n".join(tree_lines)
        return f"### {title}\n\n```text\n{diagram}\n```"

    # --------------------
    # ASCII Rendering Helpers
    # --------------------

    def _ascii_chart(
        self,
        x: List[Any],
        y: List[float],
        title: str,
        chart_type: str,
        x_label: str,
        y_label: str,
    ) -> str:
        """Render ASCII chart (bar or line)."""
        if not y:
            return f"### {title}\n\nNo data to display."

        height = self.valves.chart_height
        width = self.valves.chart_width

        if chart_type == "bar":
            return self._ascii_bar_chart(x, y, title, height, width)
        else:  # line or scatter
            return self._ascii_line_chart(x, y, title, height, width)

    def _ascii_bar_chart(
        self,
        x: List[Any],
        y: List[float],
        title: str,
        height: int,
        width: int,
    ) -> str:
        """Render ASCII bar chart."""
        if not y:
            return f"### {title}\n\nNo data."

        max_y = max(y) if y else 1
        min_y = min(y) if y else 0
        range_y = max_y - min_y if max_y != min_y else 1

        # Build chart
        lines = [f"### {title}", "", "```"]

        # Y-axis labels and bars
        bar_chars = "█▇▆▅▄▃▂▁"

        for i in range(height, 0, -1):
            threshold = min_y + (range_y * i / height)
            y_label = f"{threshold:6.1f} │"

            bars = ""
            for val in y:
                if val >= threshold:
                    bars += "█"
                elif val >= threshold - (range_y / height):
                    # Partial bar
                    frac = (val - (threshold - range_y / height)) / (range_y / height)
                    idx = min(int(frac * len(bar_chars)), len(bar_chars) - 1)
                    bars += bar_chars[idx]
                else:
                    bars += " "
                bars += " "

            lines.append(y_label + bars)

        # X-axis
        lines.append(" " * 8 + "┴" * (len(y) * 2))

        # X labels (truncated if too long)
        x_labels = ""
        for label in x:
            label_str = str(label)[:3]
            x_labels += f"{label_str:^3} "
        lines.append(" " * 8 + x_labels)

        lines.append("```")
        return "\n".join(lines)

    def _ascii_line_chart(
        self,
        x: List[Any],
        y: List[float],
        title: str,
        height: int,
        width: int,
    ) -> str:
        """Render ASCII line chart."""
        if not y:
            return f"### {title}\n\nNo data."

        max_y = max(y) if y else 1
        min_y = min(y) if y else 0
        range_y = max_y - min_y if max_y != min_y else 1

        # Create grid
        grid = [[" " for _ in range(len(y) * 2)] for _ in range(height)]

        # Plot points
        for i, val in enumerate(y):
            if range_y > 0:
                row = int((max_y - val) / range_y * (height - 1))
                row = max(0, min(height - 1, row))
            else:
                row = height // 2

            col = i * 2
            grid[row][col] = "●"

            # Connect with previous point
            if i > 0:
                prev_val = y[i - 1]
                if range_y > 0:
                    prev_row = int((max_y - prev_val) / range_y * (height - 1))
                    prev_row = max(0, min(height - 1, prev_row))
                else:
                    prev_row = height // 2

                # Draw line between points
                start_row, end_row = sorted([prev_row, row])
                for r in range(start_row, end_row + 1):
                    if r != prev_row and r != row:
                        grid[r][col - 1] = "│"

                if prev_row == row:
                    grid[row][col - 1] = "─"

        # Build output
        lines = [f"### {title}", "", "```"]

        for i, row in enumerate(grid):
            threshold = max_y - (range_y * i / (height - 1))
            y_label = f"{threshold:6.1f} │"
            lines.append(y_label + "".join(row))

        # X-axis
        lines.append(" " * 8 + "┴" * (len(y) * 2))

        # X labels
        x_labels = ""
        for label in x:
            label_str = str(label)[:3]
            x_labels += f"{label_str:^3} "
        lines.append(" " * 8 + x_labels)

        lines.append("```")
        return "\n".join(lines)

    def _ascii_heatmap(
        self,
        data: List[List[float]],
        row_labels: List[str],
        col_labels: List[str],
        title: str,
    ) -> str:
        """Render ASCII heatmap using block characters."""
        if not data:
            return f"### {title}\n\nNo data."

        # Find min/max for normalization
        all_vals = [val for row in data for val in row]
        min_val = min(all_vals) if all_vals else 0
        max_val = max(all_vals) if all_vals else 1
        range_val = max_val - min_val if max_val != min_val else 1

        # Block characters from light to dark
        blocks = " ░▒▓█"

        lines = [f"### {title}", "", "```"]

        # Header with column labels
        header = " " * 12 + " ".join(f"{str(c)[:8]:^8}" for c in col_labels)
        lines.append(header)
        lines.append("")

        # Rows
        for i, row_label in enumerate(row_labels):
            if i >= len(data):
                continue

            row_str = f"{str(row_label)[:10]:10} │"
            for j, col_label in enumerate(col_labels):
                if j >= len(data[i]):
                    row_str += "   "
                    continue

                val = data[i][j]
                normalized = (val - min_val) / range_val if range_val > 0 else 0.5
                block_idx = min(int(normalized * len(blocks)), len(blocks) - 1)
                row_str += f" {blocks[block_idx] * 2}  "

            lines.append(row_str)

        lines.append("")
        lines.append(f"Range: {min_val:.2f} to {max_val:.2f}")
        lines.append("```")

        return "\n".join(lines)

    # --------------------
    # General Helpers
    # --------------------

    def _resolve_mode(self, mode: OutputMode) -> Literal["embed", "text"]:
        """Resolve 'auto' mode to either 'embed' or 'text'."""
        if mode == "auto":
            return (
                "embed"
                if (self.valves.allow_external_cdn and HTMLResponse is not None)
                else "text"
            )
        return mode  # type: ignore

    def _find_best_values(
        self, criteria: List[str], scores: Dict[str, Dict[str, Any]], items: List[str]
    ) -> Dict[str, set]:
        """Find best values for each numeric criterion (higher is better)."""
        best_values = {}

        for criterion in criteria:
            values = []
            for item in items:
                val = scores.get(item, {}).get(criterion)
                if isinstance(val, (int, float)):
                    values.append(val)

            if values:
                max_val = max(values)
                best_values[criterion] = {max_val}

        return best_values

    def _markdown_table(
        self, title: str, cols: List[str], rows: List[Dict[str, Any]]
    ) -> str:
        def cell(v: Any) -> str:
            s = "" if v is None else str(v)
            return s.replace("\n", " ").strip()

        header = "| " + " | ".join(cols) + " |"
        sep = "| " + " | ".join(["---"] * len(cols)) + " |"
        body_lines = []
        for r in rows:
            body_lines.append(
                "| " + " | ".join(cell(r.get(c, "")) for c in cols) + " |"
            )

        result = f"{header}\n{sep}\n" + "\n".join(body_lines)
        if title:
            result = f"### {title}\n\n" + result
        return result

    def _html_table(
        self, title: str, cols: List[str], rows: List[Dict[str, Any]]
    ) -> str:
        def esc(v: Any) -> str:
            s = "" if v is None else str(v)
            return html.escape(s)

        # Detect numeric columns for right-alignment
        numeric_cols = set()
        for col in cols:
            if all(
                isinstance(r.get(col), (int, float)) or r.get(col) is None for r in rows
            ):
                numeric_cols.add(col)

        ths = "".join(
            f"<th style='text-align:{'right' if c in numeric_cols else 'left'};padding:8px;border-bottom:1px solid #ddd;'>{esc(c)}</th>"
            for c in cols
        )
        trs = []
        for r in rows:
            tds = "".join(
                f"<td style='padding:8px;border-bottom:1px solid #f0f0f0;vertical-align:top;text-align:{'right' if c in numeric_cols else 'left'};'>{esc(r.get(c, ''))}</td>"
                for c in cols
            )
            trs.append(f"<tr>{tds}</tr>")

        return f"""
<div style="font-family: system-ui, -apple-system, Segoe UI, Roboto, sans-serif;">
  <h3 style="margin:0 0 10px 0;">{html.escape(title)}</h3>
  <div style="overflow:auto;border:1px solid #eee;border-radius:10px;">
    <table style="border-collapse:collapse;width:100%;min-width:480px;">
      <thead><tr>{ths}</tr></thead>
      <tbody>
        {''.join(trs)}
      </tbody>
    </table>
  </div>
</div>
"""

    def _html_comparison_table(
        self,
        title: str,
        cols: List[str],
        rows: List[Dict[str, Any]],
        best_values: Dict[str, set],
    ) -> str:
        def esc(v: Any) -> str:
            s = "" if v is None else str(v)
            return html.escape(s)

        # Detect numeric columns
        numeric_cols = set()
        for col in cols:
            if col == "Item":
                continue
            if all(
                isinstance(r.get(col), (int, float)) or r.get(col) is None for r in rows
            ):
                numeric_cols.add(col)

        ths = "".join(
            f"<th style='text-align:{'right' if c in numeric_cols else 'left'};padding:8px;border-bottom:2px solid #ddd;font-weight:600;'>{esc(c)}</th>"
            for c in cols
        )

        trs = []
        for r in rows:
            tds = []
            for c in cols:
                val = r.get(c, "")
                # Remove ★ marker for HTML (we use background color)
                display_val = str(val).replace(" ★", "")
                is_best = c in best_values and val in best_values[c]
                bg_color = "#e7f5e8" if is_best else "transparent"
                tds.append(
                    f"<td style='padding:8px;border-bottom:1px solid #f0f0f0;vertical-align:top;text-align:{'right' if c in numeric_cols else 'left'};background-color:{bg_color};'>{esc(display_val)}</td>"
                )
            trs.append(f"<tr>{''.join(tds)}</tr>")

        return f"""
<div style="font-family: system-ui, -apple-system, Segoe UI, Roboto, sans-serif;">
  <h3 style="margin:0 0 10px 0;">{html.escape(title)}</h3>
  <div style="overflow:auto;border:1px solid #eee;border-radius:10px;">
    <table style="border-collapse:collapse;width:100%;min-width:480px;">
      <thead><tr>{ths}</tr></thead>
      <tbody>
        {''.join(trs)}
      </tbody>
    </table>
  </div>
</div>
"""

    def _html_table_simple(self, cols: List[str], rows: List[Dict[str, Any]]) -> str:
        """Simplified table for dashboard components (no title)."""

        def esc(v: Any) -> str:
            s = "" if v is None else str(v)
            return html.escape(s)

        ths = "".join(
            f"<th style='text-align:left;padding:6px;border-bottom:1px solid #ddd;font-size:13px;'>{esc(c)}</th>"
            for c in cols
        )
        trs = []
        for r in rows:
            tds = "".join(
                f"<td style='padding:6px;border-bottom:1px solid #f0f0f0;font-size:13px;'>{esc(r.get(c, ''))}</td>"
                for c in cols
            )
            trs.append(f"<tr>{tds}</tr>")

        return f"""
<div style="overflow:auto;border:1px solid #eee;border-radius:8px;">
  <table style="border-collapse:collapse;width:100%;">
    <thead><tr>{ths}</tr></thead>
    <tbody>
      {''.join(trs)}
    </tbody>
  </table>
</div>
"""

    def _wrap_html(self, title: str, body: str) -> str:
        return f"""<!doctype html>
<html>
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width,initial-scale=1" />
  <title>{html.escape(title)}</title>
  <style>
    body {{ margin: 0; padding: 12px; }}
  </style>
</head>
<body>
{body}
</body>
</html>
"""
