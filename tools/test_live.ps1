param([int]$Seconds = 10, [ValidateSet(1,2,4)][int]$Format=2, [switch]$Retune)
$ErrorActionPreference = 'Stop'
if ($Seconds -lt 1 -or $Seconds -gt 120) { throw 'Seconds must be 1..120' }
$server = Start-Process -FilePath (Join-Path $PSScriptRoot 'spyserver.exe') -ArgumentList 'live=1','port=5556' -WorkingDirectory $PSScriptRoot -WindowStyle Hidden -RedirectStandardOutput (Join-Path $PSScriptRoot 'cap/live_test.log') -RedirectStandardError (Join-Path $PSScriptRoot 'cap/live_test.err') -PassThru
try {
    Start-Sleep -Seconds 2
    $client = [Net.Sockets.TcpClient]::new('127.0.0.1',5556)
    $client.ReceiveTimeout = 5000
    $stream = $client.GetStream()
    $writer = [IO.BinaryWriter]::new($stream)
    $reader = [IO.BinaryReader]::new($stream)
    $writer.Write([uint32]0); $writer.Write([uint32]4); $writer.Write([uint32]33556132)
    function Setting([uint32]$id,[uint32]$value) {
        $writer.Write([uint32]2); $writer.Write([uint32]8); $writer.Write($id); $writer.Write($value)
    }
    Setting 100 $Format
    Setting 102 0
    Setting 101 2462000000
    Setting 0 1
    Setting 1 1
    $samples = 0L; $packets = 0; $previousSeq = $null
    $clock = [Diagnostics.Stopwatch]::new()
    $hashes = [Collections.Generic.HashSet[string]]::new()
    $sha = [Security.Cryptography.SHA256]::Create()
    $rate = 0; $retuned = $false; $syncs=0
    $expectedType = switch ($Format) {1 {100} 2 {101} 4 {103}}
    $bytesPerSample = switch ($Format) {1 {2} 2 {4} 4 {8}}
    while (!$clock.IsRunning -or $clock.Elapsed.TotalSeconds -lt $Seconds) {
        $proto = $reader.ReadUInt32(); $type = $reader.ReadUInt32(); $channel = $reader.ReadUInt32(); $sequence = $reader.ReadUInt32(); $length = $reader.ReadUInt32()
        if ($proto -ne 33556132 -or $length -gt 1048576) { throw 'Invalid header' }
        if ($null -ne $previousSeq -and $sequence -ne $previousSeq+1) { throw 'Sequence discontinuity' }
        $previousSeq = $sequence
        $body = $reader.ReadBytes($length)
        if ($body.Length -ne $length) { throw 'Truncated message' }
        if ($type -eq 0) {
            $rate = [BitConverter]::ToUInt32($body,8)
            if ($rate -ne 1250000 -or [BitConverter]::ToUInt32($body,16) -ne 0) { throw 'Wrong live sample rate/decimation advertisement' }
        }
        if ($type -eq 1) {$syncs++}
        if ($type -eq $expectedType) {
            if ($length -ne 1984*$bytesPerSample) { throw 'Wrong IQ block size' }
            if (!$clock.IsRunning) { $clock.Start() }
            $samples += $length/$bytesPerSample; $packets++
            if ($packets -le 100) { [void]$hashes.Add([BitConverter]::ToString($sha.ComputeHash($body))) }
        }
        if ($Retune -and !$retuned -and $clock.Elapsed.TotalSeconds -gt $Seconds/2) {
            Setting 101 2437000000
            $retuned=$true
        }
    }
    Setting 1 0
    $measured = $samples/$clock.Elapsed.TotalSeconds
    if ($hashes.Count -ne 100) { throw 'Repeated IQ blocks' }
    if (!$Retune -and [Math]::Abs($measured/$rate-1) -gt 0.03) { throw "Wrong stream pace: $measured" }
    if ($Retune -and (!$retuned -or $syncs -lt 3)) {throw 'Retune sync missing'}
    Write-Output "PASS format=$Format retune=$retuned samples=$samples packets=$packets unique_first_100=$($hashes.Count) measured_samples_per_second=$([int]$measured)"
    $client.Dispose(); $client = $null
    Start-Sleep -Milliseconds 200
} finally {
    if ($client) { $client.Dispose() }
    if (!$server.HasExited) { Stop-Process -Id $server.Id }
}
Get-Content (Join-Path $PSScriptRoot 'cap/live_test.err')
