import json
from pathlib import Path


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
            # Clean the stem by removing the .dot suffix if it exists
            img_stem = orig_img.stem.replace(".dot", "")

            test_name = None
            func_name = None

            for name in known_test_names:
                # Check if the image stem starts with the test name followed by an underscore
                if img_stem.startswith(name + "_"):
                    test_name = name
                    # The function name is everything after the prefix
                    func_name = img_stem[len(name) + 1 :]
                    break  # Found our match, stop searching

            # If no prefix was found, it might be a test with no function suffix
            if not test_name:
                if img_stem in known_test_names:
                    test_name = img_stem
                    func_name = "main"  # Assume main if no function is specified
                else:
                    continue

            if test_name not in tests:
                tests[test_name] = {}
            obf_img = obf_dir / orig_img.name

            if obf_img.exists():
                tests[test_name][func_name] = {
                    "original": str(orig_img.relative_to(results_dir)),
                    "obfuscated": str(obf_img.relative_to(results_dir)),
                }

    # Read metrics from logs
    metrics = {}
    logs_dir = results_dir / "logs"
    if logs_dir.exists():
        for log_file in logs_dir.glob("*.log"):
            test_name = log_file.stem
            metrics[test_name] = {}  # Default to empty metrics
            with open(log_file, "r") as f:
                for line in f:
                    # Find the marker in the current line
                    if "CFF_METRICS:" in line:
                        try:
                            json_start_pos = line.find("{")
                            if json_start_pos != -1:
                                metric_json_str = line[json_start_pos:]
                                metrics[test_name] = json.loads(metric_json_str)
                                break
                        except (json.JSONDecodeError, IndexError):
                            break

    # Create HTML (rest of the HTML generation code remains the same)
    html_content = """
        <!DOCTYPE html>
        <html lang="en">
        <head>
            <meta charset="UTF-8">
            <meta name="viewport" content="width=device-width, initial-scale=1.0">
            <title>Control Flow Flattening - Visual Comparison</title>
            <style>
                * {
                    margin: 0;
                    padding: 0;
                    box-sizing: border-box;
                }

                body {
                    font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
                    background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
                    min-height: 100vh;
                    padding: 20px;
                }

                .container {
                    max-width: 1400px;
                    margin: 0 auto;
                    background: white;
                    border-radius: 20px;
                    box-shadow: 0 20px 60px rgba(0,0,0,0.3);
                    overflow: hidden;
                }

                .header {
                    background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
                    color: white;
                    padding: 30px;
                    text-align: center;
                }

                .header h1 {
                    font-size: 2.5em;
                    margin-bottom: 10px;
                }

                .header p {
                    font-size: 1.2em;
                    opacity: 0.9;
                }

                .controls {
                    padding: 20px;
                    background: #f8f9fa;
                    border-bottom: 1px solid #dee2e6;
                    display: flex;
                    gap: 20px;
                    align-items: center;
                    flex-wrap: wrap;
                }

                .controls select, .controls button {
                    padding: 10px 20px;
                    border-radius: 5px;
                    border: 1px solid #dee2e6;
                    background: white;
                    font-size: 16px;
                    cursor: pointer;
                }

                .controls button {
                    background: #667eea;
                    color: white;
                    border: none;
                    transition: background 0.3s;
                }

                .controls button:hover {
                    background: #764ba2;
                }

                .metrics {
                    padding: 20px;
                    background: #e9ecef;
                    display: none;
                }

                .metrics.show {
                    display: block;
                }

                .metrics-grid {
                    display: grid;
                    grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
                    gap: 15px;
                }

                .metric-card {
                    background: white;
                    padding: 15px;
                    border-radius: 10px;
                    text-align: center;
                }

                .metric-card .value {
                    font-size: 2em;
                    font-weight: bold;
                    color: #667eea;
                }

                .metric-card .label {
                    color: #6c757d;
                    margin-top: 5px;
                }

                .comparison-container {
                    padding: 20px;
                }

                .comparison-mode {
                    display: flex;
                    gap: 10px;
                    margin-bottom: 20px;
                    justify-content: center;
                }

                .mode-btn {
                    padding: 10px 20px;
                    border: 2px solid #667eea;
                    background: white;
                    color: #667eea;
                    border-radius: 5px;
                    cursor: pointer;
                    transition: all 0.3s;
                }

                .mode-btn.active {
                    background: #667eea;
                    color: white;
                }

                .image-container {
                    display: flex;
                    gap: 20px;
                    justify-content: center;
                    align-items: flex-start;
                    min-height: 400px;
                }

                .image-container.overlay {
                    position: relative;
                }

                .image-wrapper {
                    flex: 1;
                    text-align: center;
                }

                .image-wrapper h3 {
                    margin-bottom: 15px;
                    color: #495057;
                    font-size: 1.3em;
                }

                .image-wrapper img {
                    max-width: 100%;
                    height: auto;
                    border: 2px solid #dee2e6;
                    border-radius: 10px;
                    background: white;
                }

                .slider-container {
                    position: relative;
                    width: 100%;
                    max-width: 800px;
                    margin: 0 auto;
                }

                .slider-container img {
                    width: 100%;
                    height: auto;
                }

                .slider {
                    position: absolute;
                    top: 50%;
                    left: 50%;
                    transform: translate(-50%, -50%);
                    width: 100%;
                    cursor: ew-resize;
                }

                .no-data {
                    text-align: center;
                    padding: 50px;
                    color: #6c757d;
                    font-size: 1.2em;
                }

                .footer {
                    padding: 20px;
                    text-align: center;
                    background: #f8f9fa;
                    color: #6c757d;
                }
            </style>
        </head>
        <body>
            <div class="container">
                <div class="header">
                    <h1>🔒 Control Flow Flattening Comparison</h1>
                    <p>Visual comparison of original vs obfuscated control flow graphs</p>
                </div>

                <div class="controls">
                    <select id="testSelect">
                        <option value="">Select a test...</option>
    """

    # Add test options
    for test_name in sorted(tests.keys()):
        html_content += f'<option value="{test_name}">{test_name}</option>\n'
    html_content += (
        """
            </select>

            <select id="functionSelect" disabled>
                <option value="">Select a function...</option>
            </select>

            <button onclick="toggleMetrics()">📊 Show Metrics</button>
                </div>

                <div class="metrics" id="metrics">
                    <div class="metrics-grid" id="metricsGrid">
                        <!-- Metrics will be inserted here -->
                    </div>
                </div>

                <div class="comparison-container">
                    <div class="comparison-mode">
                        <button class="mode-btn active" onclick="setMode('side-by-side')">Side by Side</button>
                        <button class="mode-btn" onclick="setMode('overlay')">Overlay</button>
                        <button class="mode-btn" onclick="setMode('slider')">Slider</button>
                    </div>

                    <div id="imageContainer" class="image-container">
                        <div class="no-data">Select a test and function to view comparison</div>
                    </div>
                </div>

                <div class="footer">
                    <p>Generated by Chakravyuha Control Flow Flattening Pass</p>
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
                        for (const func in tests[currentTest]) {
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
                        return;
                    }

                    const images = tests[currentTest][currentFunction];

                    if (currentMode === 'side-by-side') {
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
                    } else if (currentMode === 'overlay') {
                        container.className = 'image-container overlay';
                        container.innerHTML = `
                            <div class="slider-container">
                                <img src="../../${images.original}" style="position: absolute; opacity: 0.5;">
                                <img src="../../${images.obfuscated}" style="opacity: 0.5;">
                            </div>
                        `;
                    } else if (currentMode === 'slider') {
                        container.className = 'image-container';
                        container.innerHTML = `
                            <div class="slider-container">
                                <div style="position: relative; overflow: hidden;">
                                    <img src="../../${images.original}" alt="Original">
                                    <div style="position: absolute; top: 0; left: 50%; right: 0; bottom: 0; overflow: hidden;">
                                        <img src="../../${images.obfuscated}" alt="Obfuscated" style="position: absolute; left: -50%; width: 200%;">
                                    </div>
                                    <input type="range" class="slider" min="0" max="100" value="50" oninput="updateSlider(this)">
                                </div>
                            </div>
                        `;
                    }

                    updateMetrics();
                }

                function setMode(mode) {
                    currentMode = mode;
                    document.querySelectorAll('.mode-btn').forEach(btn => {
                        btn.classList.remove('active');
                    });
                    event.target.classList.add('active');
                    updateDisplay();
                }

                function updateSlider(slider) {
                    const container = slider.parentElement;
                    const overlay = container.querySelector('div');
                    overlay.style.left = slider.value + '%';
                }
                
                function toggleMetrics() {
                    // First, always ensure the metrics content is up-to-date
                    // for the currently selected test.
                    updateMetrics();

                    // Then, toggle the visibility of the container.
                    const metricsDiv = document.getElementById('metrics');
                    metricsDiv.classList.toggle('show');
                }

                function updateMetrics() {
                    const grid = document.getElementById('metricsGrid');

                    if (currentTest && metrics[currentTest]) {
                        const m = metrics[currentTest];
                        grid.innerHTML = `
                            <div class="metric-card">
                                <div class="value">${m.flattenedFunctions || 0}</div>
                                <div class="label">Flattened Functions</div>
                            </div>
                            <div class="metric-card">
                                <div class="value">${m.flattenedBlocks || 0}</div>
                                <div class="label">Flattened Blocks</div>
                            </div>
                            <div class="metric-card">
                                <div class="value">${m.skippedFunctions || 0}</div>
                                <div class="label">Skipped Functions</div>
                            </div>
                        `;
                    } else {
                        grid.innerHTML = '<div class="metric-card"><div class="label">No metrics available</div></div>';
                    }
                }
            </script>
        </body>
        </html>
    """
    )

    # Write HTML file
    html_file = comparison_dir / "index.html"
    with open(html_file, "w") as f:
        f.write(html_content)

    print(f"Created comparison viewer at {html_file}")
    print(
        f"Found {len(tests)} tests with {sum(len(v) for v in tests.values())} functions"
    )


if __name__ == "__main__":
    create_comparison_html()
