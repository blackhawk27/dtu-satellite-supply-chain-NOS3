# gnss_gs_responder.rb
#
# Closed-loop GNSS validation responder. Polls MGR HK for SpacecraftMode and,
# while the spacecraft is in SCIENCE, emits a GENERIC_GNSS_GS_PING_CC each
# poll cycle with a monotonic sequence number. The FSW echoes the sequence
# back via GENERIC_GNSS HK as LAST_PING_SEQ; this script logs each closed
# round-trip so the GNSS-to-GS Validation dashboard can plot uplink count
# and round-trip closures over time.
#
# Run from the COSMOS Script Runner. Stop with Ctrl+C in Script Runner or
# by setting RUNNING = false externally.

POLL_INTERVAL_S = 5.0
ECHO_TIMEOUT_S  = 2.0
MGR_SCIENCE_MODE = 3

ping_seq = 0
RUNNING = true
puts "[gnss_gs_responder] starting - poll #{POLL_INTERVAL_S}s, echo timeout #{ECHO_TIMEOUT_S}s"

while RUNNING
  begin
    mode = tlm("MGR MGR_HK_TLM SPACECRAFT_MODE")
  rescue StandardError => e
    puts "[gnss_gs_responder] MGR HK not yet available: #{e.message}"
    wait(POLL_INTERVAL_S)
    next
  end

  if mode != MGR_SCIENCE_MODE
    puts "[gnss_gs_responder] mode=#{mode}; not SCIENCE, skipping ping"
    wait(POLL_INTERVAL_S)
    next
  end

  ping_seq += 1
  puts "[gnss_gs_responder] uplink GS_PING_CC seq=#{ping_seq}"
  cmd("GENERIC_GNSS GENERIC_GNSS_GS_PING_CC with PING_SEQ #{ping_seq}")

  # Wait for the FSW to echo this exact sequence back through HK.
  deadline = Time.now + ECHO_TIMEOUT_S
  echoed = false
  while Time.now < deadline
    begin
      last = tlm("GENERIC_GNSS GENERIC_GNSS_HK_TLM LAST_PING_SEQ")
      if last == ping_seq
        echoed = true
        break
      end
    rescue StandardError
      # HK not yet flowing through; keep waiting.
    end
    wait(0.1)
  end

  if echoed
    puts "[gnss_gs_responder] round-trip closed seq=#{ping_seq}"
  else
    puts "[gnss_gs_responder] timeout - seq=#{ping_seq} not echoed within #{ECHO_TIMEOUT_S}s"
  end

  wait(POLL_INTERVAL_S)
end
