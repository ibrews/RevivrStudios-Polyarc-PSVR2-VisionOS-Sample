#!/bin/bash
# Compaction-proof watchdog for the overnight build.
# WHY: phase transitions were originally driven by Claude's ScheduleWakeup loop. That loop does NOT
# survive a /compact (or session interruption) -> the build idled 7h at the phase1->phase2 handoff
# (2026-06-03). This watchdog moves the phase2->phase3 handoff to the SHELL so it survives anything.
# Claude's wakeups are now ONLY for reporting, never the mechanism for a transition.
set -u
ENG=/Users/Shared/GH/UnrealEngineVisionOS
STATUS=/tmp/overnight_status.txt
WLOG=/tmp/overnight_watchdog.log
say(){ echo "[$(date '+%m-%d %H:%M:%S')] WATCHDOG: $*" >> "$WLOG"; }
say "started (pid $$)"
deadline=$(( $(date +%s) + 6*3600 ))   # 6h hard stop, then give up
while [ "$(date +%s)" -lt "$deadline" ]; do
  s=$(cat "$STATUS" 2>/dev/null)
  # phase2 SUCCEEDED (final status contains 'ALL DONE ... installed=') -> launch phase3 once
  if echo "$s" | grep -q 'ALL DONE' && echo "$s" | grep -q 'installed=' && [ ! -f /tmp/overnight_phase3_wrap.log ]; then
    say "phase2 complete -> launching phase3 | $s"
    cd "$ENG" && nohup caffeinate -i bash /tmp/overnight_phase3.sh > /tmp/overnight_phase3_wrap.log 2>&1 &
    disown
    say "phase3 launched"
  fi
  # phase3 finished (success OR fail) -> watchdog done
  if echo "$s" | grep -q 'ALL DONE: ACCVR24'; then say "phase3 finished -> exit | $s"; break; fi
  # phase2 hard-failed/stopped -> do NOT launch phase3; exit and let the user/Claude decide
  if echo "$s" | grep -qiE 'P2 (FAILED|STOP)|ABORTED'; then say "phase2 failed/stopped -> exit (await user) | $s"; break; fi
  sleep 60
done
say "exited"
