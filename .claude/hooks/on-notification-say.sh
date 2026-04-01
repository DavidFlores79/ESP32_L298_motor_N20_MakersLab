#!/usr/bin/env bash
set -euo pipefail

payload="$(cat)"
message=$(echo "$payload" | jq -r '.message // ""')
title=$(echo "$payload" | jq -r '.title // ""')

# Build a descriptive speech string including title if available
if [ -n "$title" ] && [ "$title" != "null" ]; then
    speech="Attention. Notification: $title. $message"
else
    speech="Attention. Claude says: $message"
fi

# Speak it using Windows PowerShell speech synthesis
powershell.exe -Command "Add-Type -AssemblyName System.Speech; (New-Object System.Speech.Synthesis.SpeechSynthesizer).Speak('$speech')"
