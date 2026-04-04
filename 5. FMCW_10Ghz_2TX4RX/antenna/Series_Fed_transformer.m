% =========================================================================
% 4x1 Series-Fed Microstrip Patch Array - 10 GHz
% Substrate: RO4350B (Er = 3.66, Thickness = 0.51 mm)
% Orientation: Vertical (Y-axis)
% =========================================================================
clear; clc; close all;

% --- 1. Array Variables ---
f = 10e9;               % 10 GHz
c = physconst("lightspeed");
d = dielectric("EpsilonR", 3.66, "LossTangent", 0.0037, "Thickness", 510e-6);

W = c / (2 * f * sqrt((d.EpsilonR + 1) / 2));
epsilonEff = (d.EpsilonR + 1) / 2 + (d.EpsilonR - 1) / 2 / sqrt(1 + 12 * d.Thickness / W);
lambdaEff = c / (f * sqrt(epsilonEff));

L_trans = lambdaEff / 4;      % Quarter-wave length (~4.5mm)
L_conn = lambdaEff / 2;       % Half-wave spacing (~9.0mm)

deltaL = 0.412 * d.Thickness * (epsilonEff + 0.3) * (W / d.Thickness + 0.264) / ((epsilonEff - 0.258) * (W / d.Thickness + 0.8));
L = lambdaEff / 2 - 2 * deltaL;

z0 = 50;        % Main feed
zT = 70;       % Transformer (Tuning this shifts S11 depth)
zC = 40;       % Connecting lines between patches

W_feed = traceWidth(z0, d);
W_trans = traceWidth(zT, d);
W_conn = traceWidth(zC, d);

% --- 2. Geometry Construction ---
% Main Feed Line (Trunk)
feedLen = 10e-3;
t_feed = antenna.Rectangle(Length=W_feed, Width=feedLen, Center=[0, -feedLen/2]);

% Transformer Section (The Matcher)
t_match = antenna.Rectangle(Length=W_trans, Width=L_trans, Center=[0, L_trans/2]);

% Patches and Connectors
y_pos = L_trans; % Starting point after transformer
p1 = antenna.Rectangle(Length=W, Width=L, Center=[0, y_pos + L/2]);

y_pos = y_pos + L;
c1 = antenna.Rectangle(Length=W_conn, Width=L_conn, Center=[0, y_pos + L_conn/2]);

y_pos = y_pos + L_conn;
p2 = antenna.Rectangle(Length=W, Width=L, Center=[0, y_pos + L/2]);

y_pos = y_pos + L;
c2 = antenna.Rectangle(Length=W_conn, Width=L_conn, Center=[0, y_pos + L_conn/2]);

y_pos = y_pos + L_conn;
p3 = antenna.Rectangle(Length=W, Width=L, Center=[0, y_pos + L/2]);

y_pos = y_pos + L;
c3 = antenna.Rectangle(Length=W_conn, Width=L_conn, Center=[0, y_pos + L_conn/2]);

y_pos = y_pos + L_conn;
p4 = antenna.Rectangle(Length=W, Width=L, Center=[0, y_pos + L/2]);

% Combine all copper
topLayer = t_feed + t_match + p1 + c1 + p2 + c2 + p3 + c3 + p4;

% --- 3. Define Substrate and PCB Stackup ---
boardW = 35e-3; 
% FIX: Added 10mm margin to the top so the 4th patch can radiate
boardL = feedLen + (y_pos + L) + 10e-3; 

% FIX: Align ground plane center so the bottom edge perfectly matches y = -feedLen
gnd = antenna.Rectangle(Length=boardW, Width=boardL, Center=[0, -feedLen + boardL/2]);

arrPCB = pcbStack;
arrPCB.Name = '10GHz_4x1_Series_Array';
arrPCB.BoardShape = gnd;
arrPCB.Layers = {topLayer, d, gnd};

% FIX: Changed from 'ant' to 'arrPCB'
arrPCB.FeedLocations = [0, -feedLen, 1, 3];
arrPCB.FeedDiameter = W_feed/2;

% --- 4. Visualization and Analysis ---
% figure('Name', 'Top Layer Layout');
% show(arrPCB);
% view(0, 90); % Top-down view

% Generate a finer mesh
% figure('Name', 'Mesh');
mesh(arrPCB, 'MaxEdgeLength', 2.5e-3);

% Plot Return Loss
% FIX: Increased points to 21 so you don't miss a narrow notch
% freqRange = linspace(9e9, 11e9, 21);
% spar = sparameters(arrPCB, freqRange, z0);
% figure('Name', 'S11 Return Loss');
% rfplot(spar);

% What you want to see: A relatively even distribution of red/yellow across all four patches.
figure('Name', 'Current Distribution');
current(arrPCB, 10e9); % Use your exact resonant frequency here

% Plot 3D Radiation Pattern (Optional - Uncomment to view)
figure('Name', '3D Radiation Pattern');
pattern(arrPCB, 10e9);

% =========================================================================
% Helper Functions (Must be at the very bottom of the script)
% =========================================================================
function width = traceWidth(z,d)
    A = z/60 * sqrt((d.EpsilonR+1)/2) + (d.EpsilonR-1)/(d.EpsilonR+1) * (0.23 + 0.11/d.EpsilonR);
    width = d.Thickness * 8 * exp(A) / (exp(2*A)-2);
end