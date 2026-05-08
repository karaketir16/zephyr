set pagination off
set confirm off
set print pretty on

target extended-remote :3333

define halt
  monitor halt
end

define loadram
  monitor halt
  monitor arm mcr 15 0 1 0 0 0
  load
  set $pc = &_vector_table
end

define run
  loadram
  continue
end

define rerun
  monitor halt
  set $pc = &_vector_table
  continue
end

define regs
  info registers
end

display/i $pc
set remotetimeout 10
