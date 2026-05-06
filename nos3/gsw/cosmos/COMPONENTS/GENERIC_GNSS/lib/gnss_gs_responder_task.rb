# gnss_gs_responder_task.rb
#
# Auto-arming counterpart to procedures/gnss_gs_responder.rb. Runs as a COSMOS
# CmdTlmServer BACKGROUND_TASK so the operator does not have to remember to
# launch the script in Script Runner during a Denmark overfly.
#
# Trigger model:
#   - The FSW emits GENERIC_GNSS_SAT_HELLO_TLM at 1 Hz only while the GNSS
#     position is inside the Denmark geofence (lat 55-58 N, lon 8-16 E).
#   - When this task sees a fresh hello (HELLO_SEQ advanced within the last
#     LOS_TIMEOUT_S), it transitions ACQ and starts uplinking GS_PING_CC.
#   - When >= LOS_TIMEOUT_S have elapsed since the last new hello, it
#     transitions LOS, stops uplinking, and waits for the next pass.
#
# The manual procedures/gnss_gs_responder.rb is left untouched. Both can run
# concurrently; the FSW only tracks the most recent ping sequence so duplicate
# uplinks are harmless.

require "cosmos"
require "cosmos/tools/cmd_tlm_server/background_task"
require "json"

module Cosmos
  class GnssGsResponderTask < BackgroundTask
    GNSS_GS_RESPONDER_TASK_NAME = "GNSS GS Responder"

    POLL_INTERVAL_S  = 5.0
    # 5.0 covers the long-tail GENERIC_GNSS HK cycle: 1 Hz nominal HK +
    # occasional missed mirror packets that force a 2-cycle wait, plus
    # FSW SB / TO_LAB / god_view -> cosmos DEBUG transit (~0.3-0.5 s).
    # Empirical distribution from a 16-ping pass at 2026-05-03T04:14 UTC
    # showed successful echoes at 438 / 612 / 713 / 1028 / 1515 / 1621 /
    # 1713 / 2016 / 2024 / 2931 ms, with 6 outliers > 3 s (deadline-cut).
    # Bumping the ceiling above the 2931 ms 90th-percentile success and
    # below the outer POLL_INTERVAL_S keeps the steady-state ping cadence
    # at ~6 s while eliminating the deadline-edge timeouts.
    ECHO_TIMEOUT_S   = 5.0
    LOS_TIMEOUT_S    = 10.0
    # COSMOS converts SPACECRAFT_MODE bytes to the STATE name via the MGR
    # packet definition, so compare against the string the converter returns
    # rather than the numeric mode (3) the FSW header defines.
    MGR_SCIENCE_MODE_NAME = "SCIENCE"

    def call
      @break  = false
      @name   = GNSS_GS_RESPONDER_TASK_NAME
      @status = "Idle"

      ping_seq           = 0
      last_hello_seq     = nil
      last_hello_time    = nil
      seeded_hello_seq   = false
      state              = :idle

      Logger.info "#{@name}: started, watching SAT_HELLO with #{LOS_TIMEOUT_S}s LOS timeout"

      until @break
        begin
          hello_seq = tlm("GENERIC_GNSS GENERIC_GNSS_SAT_HELLO_TLM HELLO_SEQ")

          if !seeded_hello_seq
            # First read after startup. The packet default value is 0 even if
            # the FSW has never transmitted, so we cannot treat this as a real
            # hello. Stash it as the baseline; only a subsequent change counts
            # as a new beacon.
            last_hello_seq   = hello_seq
            seeded_hello_seq = true
          elsif hello_seq && hello_seq != last_hello_seq
            last_hello_seq  = hello_seq
            last_hello_time = Time.now
            if state == :idle
              state    = :active
              ping_seq = 0
              @status  = "Active (in Denmark FOV)"
              Logger.info "#{@name}: ACQ, hello_seq=#{hello_seq}, beginning ping uplink"
            end
          end
        rescue StandardError => e
          # SAT_HELLO not yet seen this session: treat as no fresh hello.
          Logger.debug "#{@name}: SAT_HELLO unavailable: #{e.message}"
        end

        if state == :active && last_hello_time &&
           (Time.now - last_hello_time) >= LOS_TIMEOUT_S
          state   = :idle
          @status = "Idle (LOS - no SAT_HELLO for #{LOS_TIMEOUT_S}s)"
          Logger.info "#{@name}: LOS, last hello was #{(Time.now - last_hello_time).round(1)}s ago"
        end

        if state == :active
          send_one_ping(ping_seq + 1)
          ping_seq += 1
        end

        sleep(POLL_INTERVAL_S)
      end

      @status = "Stopped"
      Logger.info "#{@name}: stopped"
    end

    def stop
      @break = true
    end

    private

    def send_one_ping(seq)
      mode = nil
      begin
        mode = tlm("MGR MGR_HK_TLM SPACECRAFT_MODE")
      rescue StandardError => e
        Logger.warn "#{@name}: MGR HK unavailable, sending ping anyway: #{e.message}"
      end

      if mode && mode.to_s != MGR_SCIENCE_MODE_NAME
        Logger.info "#{@name}: in FOV but mode=#{mode} (not #{MGR_SCIENCE_MODE_NAME}), holding ping seq=#{seq}"
        return
      end

      Logger.info "#{@name}: uplink GS_PING_CC seq=#{seq}"
      # FSW reads PING_SEQ as little-endian on the wire; COSMOS 4.5 always
      # encodes APPEND_PARAMETER as big-endian and ignores per-param overrides,
      # so byte-swap here before sending.
      v = seq & 0xFFFFFFFF
      seq_be = ((v & 0xFF) << 24) | ((v & 0xFF00) << 8) | ((v & 0xFF0000) >> 8) | ((v >> 24) & 0xFF)

      begin
        cmd("GENERIC_GNSS GENERIC_GNSS_GS_PING_CC with PING_SEQ #{seq_be}")
      rescue StandardError => e
        Logger.error "#{@name}: cmd dispatch failed seq=#{seq}: #{e.message}"
        return
      end
      send_time = Time.now

      # Source-of-truth uplink record for the Kibana "Uplinks" / "Uplink rate"
      # panels. Logstash reads /omni_logs/gnss_uplinks.log via a json-codec
      # file input and tags each line `gnss_ping_uplink`. Absence of a matching
      # `gnss_gs_ping_roundtrip` HK doc reveals a broken GS->FSW chain, which
      # is the dashboard's stated purpose.
      begin
        uplink_record = {
          "timestamp"     => Time.now.to_f,
          "type"          => "gs_uplink",
          "app"           => "GENERIC_GNSS",
          "msg_id"        => "0x1952",
          "function_code" => 4,
          "ping_seq"      => seq,
          "subsystem"     => "GNSS",
        }
        File.open("/omni_logs/gnss_uplinks.log", "a") { |f| f.puts(uplink_record.to_json) }
      rescue StandardError => log_err
        Logger.warn "#{@name}: failed to write uplink log: #{log_err.message}"
      end

      deadline = send_time + ECHO_TIMEOUT_S
      echoed   = false
      while Time.now < deadline
        begin
          last = tlm("GENERIC_GNSS GENERIC_GNSS_HK_TLM LAST_PING_SEQ")
          if last == seq
            echoed = true
            break
          end
        rescue StandardError
          # HK not yet flowing through; keep waiting.
        end
        sleep(0.1)
      end

      elapsed_ms = ((Time.now - send_time) * 1000.0).round
      if echoed
        Logger.info "#{@name}: round-trip closed seq=#{seq} (#{elapsed_ms} ms)"
      else
        Logger.warn "#{@name}: timeout, seq=#{seq} not echoed within #{ECHO_TIMEOUT_S}s (waited #{elapsed_ms} ms)"
      end
    end
  end
end
