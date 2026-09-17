# Sync local wiki/ directory to GitHub Wiki git repository
$repoDir = Split-Path -Parent $PSScriptRoot
$wikiSrc = "$repoDir\wiki"
$wikiRemote = "https://github.com/ahmetenesay/VolcanStorage.wiki.git"

Write-Host "Syncing Wiki from $wikiSrc to $wikiRemote..."

$tmpDir = Join-Path $env:TEMP "volcanstorage_wiki_sync"
if (Test-Path $tmpDir) { Remove-Item -Recurse -Force $tmpDir }

Write-Host "Cloning wiki remote..."
$cloneRes = git clone $wikiRemote $tmpDir 2>&1
if ($LASTEXITCODE -ne 0) {
    Write-Warning "Could not clone $wikiRemote directly."
    Write-Host "NOTE: GitHub requires you to create the first Wiki page via the GitHub Web UI once."
    Write-Host "1. Go to https://github.com/ahmetenesay/VolcanStorage/wiki"
    Write-Host "2. Click 'Create the first page', then Save."
    Write-Host "3. Re-run this script to automatically synchronize all markdown files!"
    exit 1
}

Write-Host "Copying markdown files to wiki repo..."
Copy-Item -Path "$wikiSrc\*" -Destination $tmpDir -Force -Recurse

git -C $tmpDir add -A
git -C $tmpDir commit -m "docs(wiki): synchronize architecture reference and guides"
git -C $tmpDir push origin master

Write-Host "Wiki synchronized successfully to GitHub Wiki!"
