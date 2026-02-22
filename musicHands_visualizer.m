clc; clear;

%% BLE Parameters
deviceName = "UNO_R4_BLE";
deviceAddr = "4148B24D-C685-4AF5-5AB9-4DAC113FA99C";
svcUUID    = "12345678-1234-5678-1234-56789abcdef0";
rxUUID     = "12345678-1234-5678-1234-56789abcdef1";
txUUID     = "12345678-1234-5678-1234-56789abcdef2";

%% Serial Encoder Setup
encoderPort = serialport("/dev/cu.usbmodem1101", 115200);
configureTerminator(encoderPort, "LF");
flush(encoderPort);

%% Fan Parameters
NUM_LEDS = 21;
HALF     = 10;

%% Target RPM
targetRPM = 200;   % default until encoder updates it

%% Hand Tracking
HANDS_FILE = '/Users/quinnpeacock/lightthing/hands.txt';
lastScale  = 1.0;   % single scale for both radiuses

%% Color Palette
NUM_COLORS = 5;
colorIdx   = 0;     % sent as 3rd byte to Arduino

%% Load Song
songPath = '/Users/quinnpeacock/lightthing/grime.wav';
[audioData, fs] = audioread(songPath);
if size(audioData, 2) == 2
    audioData = mean(audioData, 2);
end
player = audioplayer(audioData, fs);

%% Connect BLE
b = [];
try
    disp("Connecting by name...");
    b = ble(deviceName);
catch
    disp("Name failed, trying address...");
    b = ble(deviceAddr);
end
disp("Connected!");

rxChar = characteristic(b, svcUUID, rxUUID);
txChar = characteristic(b, svcUUID, txUUID);
initBytes = read(txChar);
fprintf("Arduino says: %s\n", char(initBytes'));

%% Countdown
for i = 3:-1:1
    fprintf("Starting in %d...\n", i);
    pause(1);
end
fprintf("GO!\n");
play(player);

%% Frequency Setup
chunkSamples = round(fs * 0.06);
freqs   = linspace(0, fs/2, chunkSamples/2 + 1);
bassIdx = freqs >= 20  & freqs <= 350;
midIdx  = freqs >= 1000 & freqs <= 3000;

%% --- Calibration ---
bassMax = 150;
midMax  = 23;
bassMin = 25;
midMin  = 5;

%% --- Main Loop ---
lastPayload  = uint8([0 0 0 0 0]);
bassLit      = 0;
samplePos    = 1;
totalSamples = length(audioData);
lastVolCmd   = -1;
fprintf("Running! Make sure musicHands.py is running.\n");

% optional smoothing
smoothRPM = targetRPM;

while samplePos < totalSamples

    %% --- Read hand data from file ---
    scale = lastScale;
    swipeDetected = false;
    try
        lines = readlines(HANDS_FILE);
        for k = 1:length(lines)
            line = strtrim(lines(k));
            if strlength(line) == 0, continue; end
            parts = split(line);
            if length(parts) >= 2
                label = parts(1);
                val   = parts(2);
                if label == "LEFT"
                    if val ~= "NONE" && val ~= "OUT"
                        y = str2double(val);
                        scale = max(0, min(1, 1 - y));
                        lastScale = scale;
                    end
                elseif label == "SWIPE"
                    if val == "1"
                        swipeDetected = true;
                    end
                end
            end
        end
    catch
        % ignore file errors
    end

    %% --- Handle swipe → cycle color palette ---
    if swipeDetected
        colorIdx = mod(colorIdx + 1, NUM_COLORS);
        fprintf("SWIPE! Color palette → %d\n", colorIdx);
    end

    %% --- Set system volume (right hand) ---
    vol = round(scale * 100);
    if vol ~= lastVolCmd
        system(sprintf('osascript -e "set volume output volume %d" &', vol));
        lastVolCmd = vol;
    end

    %% --- Sync check ---
    currentSample = get(player, 'CurrentSample');
    expectedSample = samplePos;
    drift = (currentSample - expectedSample) / fs;
    if drift > 0.1
        samplePos = currentSample;
        fprintf("Skipping ahead %.2f seconds to catch up\n", drift);
    end

    chunkEnd  = min(samplePos + chunkSamples - 1, totalSamples);
    chunk     = audioData(samplePos:chunkEnd);
    samplePos = samplePos + chunkSamples;

    if length(chunk) < chunkSamples
        chunk(end+1:chunkSamples) = 0;
    end

    spectrum   = abs(fft(chunk));
    spectrum   = spectrum(1:chunkSamples/2 + 1);

    bassEnergy = sqrt(mean(spectrum(bassIdx) .^ 2));
    midEnergy  = sqrt(mean(spectrum(midIdx)  .^ 2));

    %% --- Compute LED counts ---
    if bassEnergy < bassMin
        bassLit = 0;
    else
        bassLit = round(((bassEnergy / bassMax) ^ 1.2) * HALF);
    end
    bassLit = max(0, min(HALF, bassLit));

    if ~exist('smoothHigh', 'var'), smoothHigh = 0; end
    smoothHigh = 0.6 * smoothHigh + 0.4 * midEnergy;

    if smoothHigh < midMin
        highLit = 0;
    else
        highLit = round((smoothHigh / midMax) * HALF);
        highLit = max(0, min(HALF, highLit));
    end

    cap = round(scale * HALF);
    bassLit = min(bassLit, cap);
    highLit = min(highLit, cap);

    fprintf("Bass: %.1f  BassLit: %d  Mid: %.1f  HighLit: %d  (cap %d)  Color: %d\n", ...
        bassEnergy, bassLit, midEnergy, highLit, cap, colorIdx);

    %% --- Read encoder RPM from serial ---
    if encoderPort.NumBytesAvailable > 0
        line = readline(encoderPort);
        if startsWith(line, "RPM:")
            val = extractAfter(line, "RPM:");
            newRPM = str2double(val);

            if ~isnan(newRPM) && newRPM > 0 && newRPM < 3000
                % smooth to reduce jitter
                smoothRPM = 0.7 * smoothRPM + 0.3 * newRPM;
                targetRPM = round(smoothRPM);
            end
        end
    end

    %% --- Build 5-byte payload including RPM ---
    rpmInt = uint16(targetRPM);
    rpmLow  = bitand(rpmInt, 255);
    rpmHigh = bitshift(rpmInt, -8);
    payload = uint8([bassLit, highLit, colorIdx, rpmLow, rpmHigh]);

    if any(payload ~= lastPayload)
        write(rxChar, payload, 'uint8');
        lastPayload = payload;
    end

    pause(chunkSamples / fs);
end

fprintf("Song finished!\n");
stop(player);