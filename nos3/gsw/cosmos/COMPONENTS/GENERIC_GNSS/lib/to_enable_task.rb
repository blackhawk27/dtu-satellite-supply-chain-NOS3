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
# Destination IP (host-independent, no hardcoded gateway):
#   TO_LAB's dest_IP field is char[16], so a long hostname will not fit. The
#   correct value is the spacecraft-bridge gateway (the host's address on the
#   nos3-sc01 network, where TO_LAB telemetry reaches the host listener).
#   resolve_dest_ip tries three sources in order, so the run works even when
#   the host never wrote the override file:
#     1. /omni_logs/.to_lab_dest_ip  - the Makefile-computed value, if present
#        and non-empty (omni_logs is bind-mounted rw into the COSMOS container).
#     2. Self-resolve from the FSW. COSMOS already commands the FSW by its
#        nos3-sc01 alias (FSW_HOST), so we resolve that name and take .1 of its
#        /24, which is the spacecraft-bridge gateway on any host. This is the
#        path that makes a fresh clone work without the override file.
#     3. /proc/net/route default gateway, then TO_DEST_IP_FALLBACK as last
#        resort. NOTE: COSMOS is multi-homed (nos3-core first, then nos3-sc01),
#        so its DEFAULT route is usually nos3-core, not the spacecraft bridge;
#        the route parse is a backstop only, which is why source 2 comes first.

require "cosmos"
require "cosmos/tools/cmd_tlm_server/background_task"
require "resolv"

module Cosmos
  class ToEnableTask < BackgroundTask
    TO_ENABLE_TASK_NAME    = "TO_LAB Output Enable"

    TO_DEST_PORT           = 5013
    TO_DEST_IP_FALLBACK    = "172.21.0.1"   # last-resort default only
    TO_DEST_IP_FILE        = "/omni_logs/.to_lab_dest_ip"
    FSW_HOST               = "nos-fsw"      # nos3-sc01 alias COSMOS commands
    PROC_NET_ROUTE         = "/proc/net/route"
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

    # Resolve the TO_LAB downlink destination, host-independent. Order:
    #   1. host-written override file, 2. self-resolve from the FSW alias,
    #   3. default-route gateway, 4. hardcoded fallback.
    def resolve_dest_ip
      from_file = dest_ip_from_file
      return from_file if from_file

      from_fsw = dest_ip_from_fsw
      return from_fsw if from_fsw

      from_route = detect_bridge_gateway
      return from_route if from_route

      Logger.warn "#{@name}: no dest IP source resolved; using fallback #{TO_DEST_IP_FALLBACK}"
      TO_DEST_IP_FALLBACK
    end

    # 1. The Makefile-computed override, if the host wrote a non-empty value.
    def dest_ip_from_file
      if File.exist?(TO_DEST_IP_FILE)
        ip = File.read(TO_DEST_IP_FILE).strip
        return ip unless ip.empty?
      end
      nil
    rescue StandardError => e
      Logger.debug "#{@name}: cannot read #{TO_DEST_IP_FILE}: #{e.message}"
      nil
    end

    # 2. Resolve the FSW (the nos3-sc01 alias COSMOS already commands) and take
    #    .1 of its /24, which is the spacecraft-bridge gateway on any host.
    def dest_ip_from_fsw
      fsw_ip = Resolv.getaddress(FSW_HOST)
      octets = fsw_ip.split(".")
      return nil unless octets.length == 4

      gw = "#{octets[0]}.#{octets[1]}.#{octets[2]}.1"
      Logger.info "#{@name}: self-resolved dest from #{FSW_HOST}=#{fsw_ip} -> #{gw}"
      gw
    rescue StandardError => e
      Logger.debug "#{@name}: cannot resolve #{FSW_HOST}: #{e.message}"
      nil
    end

    # 3. Backstop: the default-route gateway from /proc/net/route. Note this is
    #    usually the nos3-core gateway here (COSMOS is multi-homed), so it is a
    #    last-ditch source, tried only after the FSW self-resolve above.
    def detect_bridge_gateway
      File.foreach(PROC_NET_ROUTE).drop(1).each do |line|
        f = line.split
        next unless f.length >= 4
        next unless f[1] == "00000000" && (f[3].to_i(16) & 0x2) != 0

        gw = f[2].to_i(16)
        return [gw].pack("V").unpack("C4").join(".")
      end
      nil
    rescue StandardError => e
      Logger.debug "#{@name}: gateway detection failed: #{e.message}"
      nil
    end
  end
end
