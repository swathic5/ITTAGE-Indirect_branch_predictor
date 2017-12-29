# ITTAGE-Indirect_branch_predictor
ITTAGE - state of the art indirect branch predictor implementation in C++

This is the infrastructure for the branch prediction competition for fall 2017, CSCE 614 course.
This infrastructure was updated to include a default indirect branch predictor. The competition was to replace this default predictor with the best indirect branch predictor. I implemented ITTAGE as it was the state of the art branch predictor. The results are detailed in the report. 

To run the predictor
1) Go to cbp2-infrastructure-v3/src and do make. 
2) Download traces from into cbp2-infrastructure-v3
3) Use the run script in cbp2-infrastructure-v3. Do - ./run.sh traces/
