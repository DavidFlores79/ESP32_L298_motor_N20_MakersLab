# PowerShell version for Windows
param()

$payload = [Console]::In.ReadToEnd() | ConvertFrom-Json

$message = if ($null -ne $payload.message) { $payload.message } else { "" }
$title = if ($null -ne $payload.title) { $payload.title } else { "" }

# Build a descriptive speech string including title if available
if ($title -and $title -ne "null") {
    $speech = "Attention. Notification: $title. $message"
} else {
    $speech = "Attention. Claude says: $message"
}

# Speak it using Windows Speech Synthesis
Add-Type -AssemblyName System.Speech
$synthesizer = New-Object System.Speech.Synthesis.SpeechSynthesizer
$synthesizer.Speak($speech)
