(module
 (func "one"
  (loop $label$2
   (br_if $label$2
    (block $label$3 (result i32)
     (drop
      (br_if $label$3
       (i32.const 0)
       (i32.load
        (i32.const 3060)
       )
      )
     )
     (i32.const 0)
    )
   )
  )
  (unreachable)
 )
 (func "two" (param $var$0 i32) (param $var$1 i32) (result i32)
  (nop)
  (nop)
  (nop)
  (nop)
  (nop)
  (if
   (i32.const 0)
   (i32.store8
    (i32.const 8)
    (block $label$2 (result i32)
     (drop
      (br_if $label$2
       (i32.const 1)
       (i32.const 0)
      )
     )
     (if
      (i32.const 0)
      (drop
       (br_if $label$2
        (i32.const 1)
        (i32.const 1)
       )
      )
     )
     (block $label$4
      (br_if $label$4
       (i32.const 0)
      )
      (br_if $label$4
       (i32.const 0)
      )
      (drop
       (br_if $label$2
        (i32.const 1)
        (i32.const 0)
       )
      )
     )
     (i32.const 6704)
    )
   )
  )
  (nop)
  (i32.const 0)
 )
 (func "use-var" (param $var$0 i64) (param $var$1 i32) (result f64)
  (local $var$2 i32)
  (block $label$1
   (br_table $label$1 $label$1 $label$1 $label$1 $label$1 $label$1 $label$1 $label$1 $label$1 $label$1
    (i32.wrap/i64
     (if (result i64)
      (i32.const 0)
      (i64.const 1)
      (if (result i64)
       (if (result i32)
        (i32.const 0)
        (unreachable)
        (block $label$6 (result i32)
         (block $label$7
          (loop $label$8
           (br_if $label$8
            (br_if $label$6
             (tee_local $var$2
              (block $label$9 (result i32)
               (get_local $var$1)
              )
             )
             (i32.const 0)
            )
           )
           (loop $label$10
            (if
             (i32.const 0)
             (set_local $var$2
              (get_local $var$1)
             )
            )
           )
           (drop
            (i32.eqz
             (get_local $var$2)
            )
           )
          )
         )
         (unreachable)
        )
       )
       (unreachable)
       (i64.const 1)
      )
     )
    )
   )
  )
  (unreachable)
 )
)

