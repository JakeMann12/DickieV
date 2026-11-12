# Show the agent instructions directly from the shell.
# Run from anywhere inside the DickieV repo:
#   powershell -ExecutionPolicy Bypass -File .agent\instructions.ps1
# or add the `.agent` directory to your PATH and run:
#   agent show

$repoRoot = Split-Path -Parent $PSScriptRoot
$instructions = Join-Path $repoRoot ".agent\AGENT_INSTRUCTIONS.md"

if (Test-Path $instructions) {
    Get-Content $instructions -Raw
} else {
    Write-Host "ERROR: Could not find .agent\AGENT_INSTRUCTIONS.md at $instructions"
    exit 1
}
