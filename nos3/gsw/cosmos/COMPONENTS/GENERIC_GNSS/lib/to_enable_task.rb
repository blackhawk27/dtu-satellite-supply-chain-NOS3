# to_enable_task.rb
#
# COSMOS CmdTlmServer BACKGROUND_TASK that owns the TO_LAB telemetry-output
# enable. This replaces the enable that god_view_capture.py used to inject:
# the ground software is the legitimate owner of the single TO_LAB downlink
# destination, so it (not a passive logger) tells TO_LAB where to forward.
#
# Model:
#   - At startup, and then every RE_ASSERT_INTERVAL_S, send
#     TO_DEBUG_ENABLE_OUTPUT_CC (CCSDS MID 0x18E8, FC=2) pointing TO_LAB at the
#     host passive_listener.py on UDP 5013.
#   - passive_listener.py decodes the stream and mirrors it to the COSMOS DEBUG
#     interface (host 5113 -> cosmos:5013), so the GNSS GS Responder still sees
#     SAT_HELLO. The listener is purely passive; this task is the only enabler.
#
# Destination IP:
#   TO_LAB's dest_IP field is char[16], so a long hostname will not fit. The
#   correct value is the nos3-sc01 bridge gateway (the host's address on the
#   spacecraft network, where TO_LAB telemetry reaches the host listener). The
#   Makefile computes it on the host and writes it to /omni_logs/.to_lab_dest_ip
#   (omni_logs is bind-mounted rw into the COSMOS container). We poll for that
#   file and fall back to TO_DEST_IP_FALLBACK if it is absent.

require "cosmos"
require "cosmos/tools/cmd_tlm_server/background_task"

module Cosmos
  class ToEnableTask < BackgroundTask
    TO_ENABLE_TASK_NAME    = "TO_LAB Output Enable"

    TO_DEST_PORT           = 5013
    TO_DEST_IP_FALLBACK    = "172.21.0.1"   # nos3-sc01 bridge gateway default
    TO_DEST_IP_FILE        = "/omni_logs/.to_lab_dest_ip"
    RE_ASSERT_INTERVAL_S   = 60.0           # low-rate re-assert; idempotent
    STARTUP_RETRY_S        = 2.0            # while waiting for the dest-IP file

    def call
      @break  = false
      @name   = TO_ENABLE_TASK_NAME
      @status = "Starting"

      Logger.info "#{@name}: started, will enable TO_LAB output toward the passive listener"

      first_enable_done = false
      until @break
        dest_ip = resolve_dest_ip

        begin
          cmd("TO_DEBUG TO_DEBUG_ENABLE_OUTPUT_CC with " \
              "DEST_IP '#{dest_ip}', DEST_PORT #{TO_DEST_PORT}")
          unless first_enable_done
            Logger.info "#{@name}: TO_LAB output enabled -> #{dest_ip}:#{TO_DEST_PORT}"
            first_enable_done = true
          end
          @status = "Enabled -> #{dest_ip}:#{TO_DEST_PORT}"
        rescue StandardError => e
          @status = "Enable failed: #{e.message}"
          Logger.warn "#{@name}: TO enable cmd failed (dest #{dest_ip}): #{e.message}"
        end

        # Sleep in short slices so stop() is responsive.
        slept = 0.0
        while !@break && slept < RE_ASSERT_INTERVAL_S
          sleep(1.0)
          slept += 1.0
        end
      end

      @status = "Stopped"
      Logger.info "#{@name}: stopped"
    end

    def stop
      @break = true
    end

    private

    # Read the host-computed destination IP if present, else use the fallback.
    def resolve_dest_ip
      begin
        if File.exist?(TO_DEST_IP_FILE)
          ip = File.read(TO_DEST_IP_FILE).strip
          return ip unless ip.empty?
        end
      rescue StandardError => e
        Logger.debug "#{@name}: cannot read #{TO_DEST_IP_FILE}: #{e.message}"
      end
      TO_DEST_IP_FALLBACK
    end
  end
end
