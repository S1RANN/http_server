#!/bin/bash

start=`date +%s.%N`

for i in {1..12};do
(
  for j in {1..5000};do
    curl -s -o /dev/null http://localhost:8080/
  done
) &
done

wait

end=`date +%s.%N`

echo "Time taken: $(echo "$end - $start" | bc) s"
