param(
    [string]$Port = "COM8",
    [int]$Baud = 2000000,
    [string]$InitialCommand = "status",
    [int]$ReadSleepMs = 50,
    [string[]]$StartupCommands = @(),
    [int]$DurationSec = 0
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$serialPort = [System.IO.Ports.SerialPort]::new($Port, $Baud, [System.IO.Ports.Parity]::None, 8, [System.IO.Ports.StopBits]::One)
$serialPort.NewLine = "`n"
$serialPort.ReadTimeout = 200
$serialPort.WriteTimeout = 500
$serialPort.DtrEnable = $true
$serialPort.RtsEnable = $true

$bestFps = 0.0
$buffer = ""
$quit = $false
$interactiveConsole = $true
$startedAt = Get-Date

if (($StartupCommands.Count -eq 1) -and ($StartupCommands[0] -like "*,*")) {
    $StartupCommands = $StartupCommands[0].Split(",") | ForEach-Object { $_.Trim() } | Where-Object { -not [string]::IsNullOrWhiteSpace($_) }
}

function Send-Command {
    param(
        [System.IO.Ports.SerialPort]$SerialPort,
        [string]$Command
    )

    if ([string]::IsNullOrWhiteSpace($Command)) {
        return
    }

    $SerialPort.WriteLine($Command)
    Write-Host ("TX  {0}" -f $Command) -ForegroundColor Cyan
}

function Print-Help {
    Write-Host "Keys:" -ForegroundColor Yellow
    Write-Host "  c  -> mode capture"
    Write-Host "  u  -> mode uart"
    Write-Host "  s  -> start"
    Write-Host "  x  -> stop"
    Write-Host "  r  -> reset counters"
    Write-Host "  t  -> status"
    Write-Host "  0  -> sleep 0"
    Write-Host "  1  -> sleep 100"
    Write-Host "  4  -> sleep 400"
    Write-Host "  h  -> help"
    Write-Host "  q  -> quit"
}

function Handle-Line {
    param(
        [string]$Line
    )

    if ([string]::IsNullOrWhiteSpace($Line)) {
        return
    }

    $timestamp = Get-Date -Format "HH:mm:ss.fff"
    if ($Line -match 'fps=([0-9]+\.[0-9]+|[0-9]+)') {
        $fps = [double]$Matches[1]
        if ($fps -gt $script:bestFps) {
            $script:bestFps = $fps
        }
        Write-Host ("[{0}] {1} best_fps={2:N2}" -f $timestamp, $Line, $script:bestFps) -ForegroundColor Green
        return
    }

    if ($Line.StartsWith("WARN") -or $Line.StartsWith("ERR")) {
        Write-Host ("[{0}] {1}" -f $timestamp, $Line) -ForegroundColor Red
        return
    }

    Write-Host ("[{0}] {1}" -f $timestamp, $Line)
}

try {
    $serialPort.Open()
    Start-Sleep -Milliseconds 1500
    $serialPort.DiscardInBuffer()
    $serialPort.DiscardOutBuffer()

    Write-Host ("Opened {0} @ {1}" -f $Port, $Baud) -ForegroundColor Yellow
    Print-Help

    if (-not [string]::IsNullOrWhiteSpace($InitialCommand)) {
        Send-Command -SerialPort $serialPort -Command $InitialCommand
    }

    foreach ($startupCommand in $StartupCommands) {
        Send-Command -SerialPort $serialPort -Command $startupCommand
        Start-Sleep -Milliseconds 150
    }

    while ($true) {
        if (($DurationSec -gt 0) -and (((Get-Date) - $startedAt).TotalSeconds -ge $DurationSec)) {
            break
        }

        if ($interactiveConsole) {
            try {
                while ([Console]::KeyAvailable) {
                    $key = [Console]::ReadKey($true)
                    switch ($key.KeyChar) {
                        'c' { Send-Command -SerialPort $serialPort -Command "mode capture" }
                        'u' { Send-Command -SerialPort $serialPort -Command "mode uart" }
                        's' { Send-Command -SerialPort $serialPort -Command "start" }
                        'x' { Send-Command -SerialPort $serialPort -Command "stop" }
                        'r' { Send-Command -SerialPort $serialPort -Command "reset" }
                        't' { Send-Command -SerialPort $serialPort -Command "status" }
                        '0' { Send-Command -SerialPort $serialPort -Command "sleep 0" }
                        '1' { Send-Command -SerialPort $serialPort -Command "sleep 100" }
                        '4' { Send-Command -SerialPort $serialPort -Command "sleep 400" }
                        'h' { Send-Command -SerialPort $serialPort -Command "help" }
                        'q' { break }
                        default { }
                    }

                    if ($key.KeyChar -eq 'q') {
                        $quit = $true
                        break
                    }
                }
            }
            catch [System.InvalidOperationException] {
                $interactiveConsole = $false
            }
        }

        if ($quit) {
            break
        }

        $chunk = $serialPort.ReadExisting()
        if (-not [string]::IsNullOrEmpty($chunk)) {
            $buffer += $chunk
            while ($buffer.Contains("`n")) {
                $newlineIndex = $buffer.IndexOf("`n")
                $line = $buffer.Substring(0, $newlineIndex).Trim("`r")
                if ($newlineIndex -lt ($buffer.Length - 1)) {
                    $buffer = $buffer.Substring($newlineIndex + 1)
                } else {
                    $buffer = ""
                }
                Handle-Line -Line $line
            }
        } else {
            Start-Sleep -Milliseconds $ReadSleepMs
        }
    }
}
finally {
    if ($serialPort.IsOpen) {
        $serialPort.Close()
    }
}
