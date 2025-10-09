import json
from pathlib import Path


def format_bytes(size):
    """Formats a size in bytes to a human-readable string."""
    if size is None or size < 0:
        return "N/A"
    if size == 0:
        return "0 B"

    power = 1024
    n = 0
    power_labels = {0: "B", 1: "KB", 2: "MB", 3: "GB", 4: "TB"}

    while size >= power and n < len(power_labels) - 1:
        size /= power
        n += 1

    if n == 0:
        return f"{int(size)} {power_labels[n]}"
    else:
        return f"{size:.2f} {power_labels[n]}"


def create_comparison_html():
    """Create an interactive HTML page for comparing CFGs"""

    results_dir = Path("test_results")
    viz_dir = results_dir / "visualizations"
    comparison_dir = viz_dir / "comparison"
    comparison_dir.mkdir(parents=True, exist_ok=True)

    # Collect all test results
    tests = {}
    tests_dir = Path("tests")
    known_test_names = sorted([p.stem for p in tests_dir.glob("test_*.c")])

    # Find all original visualizations
    orig_dir = viz_dir / "original"
    obf_dir = viz_dir / "obfuscated"

    if orig_dir.exists():
        for orig_img in orig_dir.glob("*.png"):
            img_stem = orig_img.stem.replace(".dot", "")
            test_name, func_name = None, None

            for name in known_test_names:
                if img_stem.startswith(name + "_"):
                    test_name = name
                    func_name = img_stem[len(name) + 1 :]
                    break

            if not test_name and img_stem in known_test_names:
                test_name = img_stem
                func_name = "main"

            if not test_name:
                continue

            if test_name not in tests:
                tests[test_name] = {}

            obf_img = obf_dir / orig_img.name
            if obf_img.exists():
                tests[test_name][func_name] = {
                    "original": str(orig_img.relative_to(results_dir)),
                    "obfuscated": str(obf_img.relative_to(results_dir)),
                }

    metrics = {}
    reports_dir = results_dir / "reports"
    binaries_dir = results_dir / "binaries"

    if reports_dir.exists():
        for test_name in known_test_names:
            report_type = None
            report_to_load = None

            # Find the best available report, preferring 'full'
            if (reports_dir / f"{test_name}_full.json").exists():
                report_to_load = reports_dir / f"{test_name}_full.json"
                report_type = "full"
            elif (reports_dir / f"{test_name}_cff.json").exists():
                report_to_load = reports_dir / f"{test_name}_cff.json"
                report_type = "cff"
            elif (reports_dir / f"{test_name}_string.json").exists():
                report_to_load = reports_dir / f"{test_name}_string.json"
                report_type = "string"

            if report_to_load:
                try:
                    with open(report_to_load, "r") as f:
                        metrics[test_name] = json.load(f)

                    # Get binary sizes safely
                    orig_size, obf_size = None, None
                    original_bin = binaries_dir / f"{test_name}_original"
                    obfuscated_bin = binaries_dir / f"{test_name}_{report_type}"

                    if original_bin.exists():
                        orig_size = original_bin.stat().st_size

                    if obfuscated_bin.exists():
                        obf_size = obfuscated_bin.stat().st_size

                    change_str = "N/A"
                    if orig_size is not None and obf_size is not None and orig_size > 0:
                        change_pct = (obf_size - orig_size) / orig_size * 100
                        change_str = f"{change_pct:+.2f}%"

                    metrics[test_name]["binary_metrics"] = {
                        "original_size": format_bytes(orig_size),
                        "obfuscated_size": format_bytes(obf_size),
                        "change_pct": change_str,
                    }

                except (json.JSONDecodeError, FileNotFoundError):
                    metrics[test_name] = {}
            else:
                metrics[test_name] = {}

    # Create HTML
    html_content = """
        <!DOCTYPE html>
        <html lang="en">
        <head>
            <meta charset="UTF-8">
            <meta name="viewport" content="width=device-width, initial-scale=1.0">
            <title>Chakravyuha - Visual Comparison Report</title>
            <style>
                * { margin: 0; padding: 0; box-sizing: border-box; }
                body {
                    font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
                    background: #f4f7f6;
                    color: #333;
                    padding: 20px;
                }
                .container {
                    max-width: 1600px;
                    margin: 0 auto;
                    background: white;
                    border-radius: 20px;
                    box-shadow: 0 10px 40px rgba(0,0,0,0.1);
                    overflow: hidden;
                }
                .header {
                    background: linear-gradient(135deg, #5A78E1 0%, #324172 100%);
                    color: white;
                    padding: 40px;
                    text-align: center;
                }
                .header h1 { font-size: 2.8em; margin-bottom: 10px; }
                .header p { font-size: 1.2em; opacity: 0.9; }
                .controls {
                    padding: 20px;
                    background: #f8f9fa;
                    border-bottom: 1px solid #dee2e6;
                    display: flex; gap: 20px; align-items: center; flex-wrap: wrap;
                }
                .controls select, .controls button {
                    padding: 12px 20px; border-radius: 8px; border: 1px solid #dee2e6;
                    background: white; font-size: 16px; cursor: pointer;
                }
                .controls button {
                    background: #5A78E1; color: white; border: none; transition: background 0.3s;
                }
                .controls button:hover { background: #324172; }
                .metrics { padding: 30px; background: #e9ecef; display: none; }
                .metrics.show { display: block; }
                .metrics-grid {
                    display: grid;
                    grid-template-columns: repeat(auto-fit, minmax(240px, 1fr));
                    gap: 20px;
                }
                .metric-card {
                    background: white; padding: 20px; border-radius: 12px;
                    text-align: center; box-shadow: 0 4px 12px rgba(0,0,0,0.05);
                }
                .metric-card .value {
                    font-size: 2.2em; font-weight: bold; color: #5A78E1;
                }
                .metric-card .label { color: #6c757d; margin-top: 8px; font-size: 0.9em; }
                .metric-card .sub-label { color: #999; font-size: 0.8em; }
                
                .metric-card.pass-list-card .value {
                    font-size: 1.1em;
                    line-height: 1.5;
                    word-break: break-all;
                    margin-top: 10px;
                }

                .comparison-container { padding: 30px; }
                .comparison-mode {
                    display: flex; gap: 10px; margin-bottom: 20px; justify-content: center;
                }
                .mode-btn {
                    padding: 10px 20px; border: 2px solid #5A78E1; background: white;
                    color: #5A78E1; border-radius: 8px; cursor: pointer; transition: all 0.3s;
                }
                .mode-btn.active { background: #5A78E1; color: white; }
                .image-container {
                    display: flex; gap: 20px; justify-content: center; align-items: flex-start; min-height: 400px;
                }
                .image-wrapper { flex: 1; text-align: center; }
                .image-wrapper h3 { margin-bottom: 15px; color: #495057; font-size: 1.4em; }
                .image-wrapper img {
                    max-width: 100%; height: auto; border: 2px solid #dee2e6;
                    border-radius: 10px; background: white;
                }
                .no-data { text-align: center; padding: 50px; color: #6c757d; font-size: 1.2em; }
                .footer { padding: 20px; text-align: center; background: #f8f9fa; color: #6c757d; }
            </style>
        </head>
        <body>
            <div class="container">
                <div class="header">
                    <h1>🔒 Chakravyuha Obfuscation Report</h1>
                    <p>Visual comparison of original vs. obfuscated control flow graphs</p>
                </div>

                <div class="controls">
                    <select id="testSelect">
                        <option value="">Select a test...</option>
    """

    for test_name in sorted(tests.keys()):
        html_content += f'<option value="{test_name}">{test_name}</option>\n'
    html_content += (
        """
            </select>
            <select id="functionSelect" disabled>
                <option value="">Select a function...</option>
            </select>
            <button onclick="toggleMetrics()">📊 Show Full Report</button>
                </div>

                <div class="metrics" id="metrics">
                    <div class="metrics-grid" id="metricsGrid">
                        <!-- Metrics will be inserted here -->
                    </div>
                </div>

                <div class="comparison-container">
                    <div class="comparison-mode">
                        <button class="mode-btn active" onclick="setMode('side-by-side')">Side by Side</button>
                    </div>
                    <div id="imageContainer" class="image-container">
                        <div class="no-data">Select a test and function to view comparison</div>
                    </div>
                </div>

                <div class="footer">
                    <p>Generated by the Chakravyuha LLVM Obfuscator</p>
                </div>
            </div>

            <script>
                const tests = """
        + json.dumps(tests, indent=4)
        + """;
                const metrics = """
        + json.dumps(metrics, indent=4)
        + """;

                let currentMode = 'side-by-side';
                let currentTest = '';
                let currentFunction = '';

                document.getElementById('testSelect').addEventListener('change', function(e) {
                    currentTest = e.target.value;
                    updateFunctionList();
                    updateDisplay();
                });

                document.getElementById('functionSelect').addEventListener('change', function(e) {
                    currentFunction = e.target.value;
                    updateDisplay();
                });

                function updateFunctionList() {
                    const funcSelect = document.getElementById('functionSelect');
                    funcSelect.innerHTML = '<option value="">Select a function...</option>';
                    if (currentTest && tests[currentTest]) {
                        funcSelect.disabled = false;
                        const sortedFunctions = Object.keys(tests[currentTest]).sort();
                        for (const func of sortedFunctions) {
                            const option = document.createElement('option');
                            option.value = func;
                            option.textContent = func.replace(/_/g, ' ').replace('.dot', '');
                            funcSelect.appendChild(option);
                        }
                    } else {
                        funcSelect.disabled = true;
                    }
                }

                function updateDisplay() {
                    const container = document.getElementById('imageContainer');
                    if (!currentTest || !currentFunction || !tests[currentTest] || !tests[currentTest][currentFunction]) {
                        container.innerHTML = '<div class="no-data">Select a test and function to view comparison</div>';
                    } else {
                        const images = tests[currentTest][currentFunction];
                        container.className = 'image-container';
                        container.innerHTML = `
                            <div class="image-wrapper">
                                <h3>Original</h3>
                                <img src="../../${images.original}" alt="Original CFG">
                            </div>
                            <div class="image-wrapper">
                                <h3>Obfuscated</h3>
                                <img src="../../${images.obfuscated}" alt="Obfuscated CFG">
                            </div>
                        `;
                    }
                    updateMetrics();
                }
                
                function setMode(mode) {
                    currentMode = mode;
                    document.querySelectorAll('.mode-btn').forEach(btn => btn.classList.remove('active'));
                    event.target.classList.add('active');
                    updateDisplay();
                }

                function toggleMetrics() {
                    updateMetrics();
                    const metricsDiv = document.getElementById('metrics');
                    metricsDiv.classList.toggle('show');
                }

                function updateMetrics() {
                    const grid = document.getElementById('metricsGrid');
                    if (currentTest && metrics[currentTest] && Object.keys(metrics[currentTest]).length > 0) {
                        const m = metrics[currentTest];
                        const obfMetrics = m.obfuscationMetrics || {};
                        const cff = obfMetrics.controlFlowFlattening || {};
                        const se = obfMetrics.stringEncryption || {};
                        const passes = obfMetrics.passesRun || [];
                        const attrs = m.outputAttributes || {};
                        const binMetrics = m.binary_metrics || {};

                        grid.innerHTML = `
                            <div class="metric-card">
                                <div class="value">${cff.flattenedFunctions || 0}</div>
                                <div class="label">Flattened Functions</div>
                            </div>
                            <div class="metric-card">
                                <div class="value">${cff.flattenedBlocks || 0}</div>
                                <div class="label">Flattened Blocks</div>
                            </div>
                            <div class="metric-card">
                                <div class="value">${se.count || 0}</div>
                                <div class="label">Strings Encrypted</div>
                                <div class="sub-label">${se.method || 'N/A'}</div>
                            </div>
                             <div class="metric-card">
                                <div class="value">${binMetrics.change_pct || 'N/A'}</div>
                                <div class="label">Executable Size Change</div>
                                <div class="sub-label">${binMetrics.original_size || '?'} -> ${binMetrics.obfuscated_size || '?'}</div>
                            </div>
                            <div class="metric-card">
                                <div class="value">${attrs.stringDataSizeChange || '0.00%'}</div>
                                <div class="label">IR String Data Size Change</div>
                                <div class="sub-label">${attrs.originalIRStringDataSize || '0'} -> ${attrs.obfuscatedIRStringDataSize || '0'}</div>
                            </div>
                             <div class="metric-card pass-list-card">
                                <div class="value">${passes.join('<br>') || 'None'}</div>
                                <div class="label">Passes Run</div>
                            </div>
                        `;
                    } else {
                        grid.innerHTML = '<div class="metric-card" style="grid-column: 1 / -1;"><div class="label">No report data available for this test. <br> Please run a test (e.g., `make test-full`) to generate reports.</div></div>';
                    }
                }
            </script>
        </body>
        </html>
    """
    )

    html_file = comparison_dir / "index.html"
    with open(html_file, "w") as f:
        f.write(html_content)

    print(f"Created comparison viewer at {html_file}")
    print(
        f"Found {len(tests)} tests with {sum(len(v) for v in tests.values())} functions"
    )


if __name__ == "__main__":
    create_comparison_html()
