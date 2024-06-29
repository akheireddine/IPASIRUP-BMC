never { /* GF"process@foo" */
accept_init:
  if
  :: (process@foo) -> goto accept_init
  :: (!(process@foo)) -> goto T0_S1
  fi;
T0_S1:
  if
  :: (process@foo) -> goto accept_init
  :: (!(process@foo)) -> goto T0_S1
  fi;
}
