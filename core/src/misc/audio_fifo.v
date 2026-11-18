// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 TinyParts
// Teil des Projekts NanoMig-Webradio, beruht auf NanoMig/Minimig (GPL-3.0).

// audio_fifo.v
//
// Zweitakt-FIFO fuer Stereo-Samples (2 x 16 Bit = 32 Bit), vom MCU gefuellt (Schreibseite,
// sysctrl-Takt) und im 48-kHz-Takt der HDMI-Seite gelesen (Lesetakt, clk_pixel).
// Die Zeiger laufen als Gray-Code ueber die Taktgrenze, der Datenspeicher ist ein
// Dual-Clock-Block-RAM (wird von Gowin als BSRAM erkannt).

module audio_fifo #(
  parameter ADDR_BITS = 11          // 2^11 = 2048 Samples = 42,7 ms bei 48 kHz
)(
  input                   reset,    // asynchron, wirkt auf beide Seiten

  // Schreibseite (MCU / sysctrl)
  input                   wr_clk,
  input                   wr_en,
  input      [31:0]       wr_data,
  output     [ADDR_BITS:0] wr_space,   // freie Plaetze, 0 .. 2^ADDR_BITS - 1 (Schreibtakt)

  // Leseseite (Audio-Takt)
  input                   rd_clk,
  input                   rd_en,     // ein Takt lang: naechstes Sample entnehmen
  output reg [31:0]       rd_data,   // gueltig im Takt nach rd_en
  output reg              rd_valid,  // rd_data stammt aus dem FIFO (sonst Unterlauf: Stille)
  output     [ADDR_BITS:0] rd_used     // Fuellstand aus Sicht der Leseseite (nur zur Diagnose)
);

localparam DEPTH = 1 << ADDR_BITS;

reg [31:0] mem [0:DEPTH-1];

// ---------------------------------------------------------------- Zeiger
reg [ADDR_BITS:0] wr_ptr = 0;       // binaer, ein Bit breiter als die Adresse
reg [ADDR_BITS:0] rd_ptr = 0;
reg [ADDR_BITS:0] wr_gray = 0, rd_gray = 0;

function [ADDR_BITS:0] bin2gray(input [ADDR_BITS:0] b);
  bin2gray = b ^ (b >> 1);
endfunction

function [ADDR_BITS:0] gray2bin(input [ADDR_BITS:0] g);
  integer i;
  begin
    gray2bin[ADDR_BITS] = g[ADDR_BITS];
    for (i = ADDR_BITS-1; i >= 0; i = i - 1)
      gray2bin[i] = gray2bin[i+1] ^ g[i];
  end
endfunction

// Zeiger in die jeweils andere Domaene bringen (2-FF-Synchronisierer)
reg [ADDR_BITS:0] rd_gray_w1, rd_gray_w2;
reg [ADDR_BITS:0] wr_gray_r1, wr_gray_r2;

always @(posedge wr_clk or posedge reset) begin
  if (reset) begin
    rd_gray_w1 <= 0; rd_gray_w2 <= 0;
  end else begin
    rd_gray_w1 <= rd_gray;
    rd_gray_w2 <= rd_gray_w1;
  end
end

always @(posedge rd_clk or posedge reset) begin
  if (reset) begin
    wr_gray_r1 <= 0; wr_gray_r2 <= 0;
  end else begin
    wr_gray_r1 <= wr_gray;
    wr_gray_r2 <= wr_gray_r1;
  end
end

// ---------------------------------------------------------------- Schreiben
wire [ADDR_BITS:0] rd_ptr_in_wr = gray2bin(rd_gray_w2);
wire [ADDR_BITS:0] wr_used = wr_ptr - rd_ptr_in_wr;
assign wr_space = DEPTH - 1 - wr_used;          // einen Platz frei lassen
wire wr_full = (wr_used >= DEPTH - 1);

always @(posedge wr_clk or posedge reset) begin
  if (reset) begin
    wr_ptr  <= 0;
    wr_gray <= 0;
  end else if (wr_en && !wr_full) begin
    wr_ptr  <= wr_ptr + 1'b1;
    wr_gray <= bin2gray(wr_ptr + 1'b1);
  end
end

always @(posedge wr_clk) begin
  if (wr_en && !wr_full)
    mem[wr_ptr[ADDR_BITS-1:0]] <= wr_data;
end

// ---------------------------------------------------------------- Lesen
wire [ADDR_BITS:0] wr_ptr_in_rd = gray2bin(wr_gray_r2);
wire [ADDR_BITS:0] rd_avail = wr_ptr_in_rd - rd_ptr;
assign rd_used = rd_avail;
wire rd_empty = (rd_avail == 0);

always @(posedge rd_clk or posedge reset) begin
  if (reset) begin
    rd_ptr   <= 0;
    rd_gray  <= 0;
    rd_valid <= 1'b0;
  end else begin
    rd_valid <= 1'b0;
    if (rd_en) begin
      if (!rd_empty) begin
        rd_ptr   <= rd_ptr + 1'b1;
        rd_gray  <= bin2gray(rd_ptr + 1'b1);
        rd_valid <= 1'b1;
      end
    end
  end
end

// synchroner Lesezugriff auf den Block-RAM (Adresse = aktueller Lesezeiger)
always @(posedge rd_clk) begin
  rd_data <= mem[rd_ptr[ADDR_BITS-1:0]];
end

endmodule
