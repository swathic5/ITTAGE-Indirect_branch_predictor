// my_predictor.h
// This file contains a sample my_predictor class.
// It has a simple 32,768-entry gshare with a history length of 15 for conditional branch predictor and
// ITTAGE implemented for indirect branches with 4K history bits, 10 tagged prediction tables and a base predictor
#include <stdio.h>
#include <stdint.h>
using namespace std;

//ITTAGE uses folded history for index computation. Define the function here since the index computation is done during both predict and update
uint8_t addr_fold(int address){
        uint8_t folded_address,k;
	folded_address = 0;
        for(k=0; k<3; k++){
        folded_address ^= ((address%(1<<((k+1)*8))) / (1<<(k*8)));
        }
        folded_address ^= address/(1<<(24));
        return folded_address;
}
//Addr_fold function complete

//Update the class with more values that can be passed from predict to update. Send data such as the index and table for pred and altpred and which component among pred, altpred and base predictor sent the data
class my_update : public branch_update {
public:
	//Conditional predictor 
	unsigned int cond_index;					//Conditional index

	//Indirect branch predictor
	//Found
	bool found;							//Atleast pred component was found. Prediction is not from base table
	bool altpred_found;						//A alternate prediction was found. Might or might not was used
	//Index
        uint8_t index;							//Index of final prediction
	uint8_t pred_index;						//Index of predictor component
	uint8_t altpred_index;						//Index of alternate predictor
	//Tag
	int history_table;						//Table from where the final prediction was send	
	int pred_history_table;						//Table where the pred was found
	int altpred_history_table;					//Table where altpred was found
	//Target
	unsigned int predicted_target;					//The final predicted target used by update policy
};

class my_predictor : public branch_predictor {
public:
#define HISTORY_LENGTH	15						//Conditional branch history bits length
#define TABLE_BITS	15						//Noumber of bits used to index conditional predictor table
#define TAG_BITS	20						//Number of bits used to index base predictor of ITTAGE
#define HISTORY_TABLES	10						//Number of predictor tables for ITTAGE
	my_update u;
	branch_info bi;

	//Conditional predictor
	unsigned int history;						//Conditional branch history bits
	unsigned char tab[1<<TABLE_BITS];                               //Conditional predictor table

	//Indirect branch predictor
	uint8_t indirect_history[(1<<(HISTORY_TABLES))+2];		//Indirect history - 4K bits for 10 predcitor tables
	uint8_t CSR1[HISTORY_TABLES];					//Circular shift register that folds 4K history bits onto 8 bits - used for index and tag computation
	uint8_t CSR2[HISTORY_TABLES]; 					//Circular shift register that folds 4K history bits onto 8 bits - used for tage computation
	unsigned int base_predictor[1<<TAG_BITS];			//Base predictor 
	unsigned int pred[HISTORY_TABLES][1<<8];			//Predictor tables 
	int valid[HISTORY_TABLES][1<<8];				//Valid bits 
	int ctr[HISTORY_TABLES][1<<8];					//Counter values 
	bool useful[HISTORY_TABLES][1<<8];				//USeful bit 
	int reset_counter;						//Reset counter 
	uint8_t tag[HISTORY_TABLES][1<<8];				//Tag bits 
	int overflow_bit;						//Overflow bit when using CSR	
	int USE_ALT_ON_NA;						//Use altpred counter
 	int previous_target;
	
	//Initialize everything	
	my_predictor (void) : history(0) {
		memset (tab, 0, sizeof (tab));
		memset (base_predictor, 0, sizeof (base_predictor));
		
		for (int l=0; l<=(HISTORY_TABLES-1);l++){
			for (int m=0; m<=((1<<8)-1);m++){
				valid[l][m] = 0;
				ctr[l][m] = 0;
				useful[l][m] = 0;
				tag[l][m]= 0;
			}
			CSR1[l]=0;
			CSR2[l]=0;
		}
		for (int l=0; l<=(1<<(HISTORY_TABLES-1)); l++){
			indirect_history[l] = 0;
		}
		reset_counter = 128;
		USE_ALT_ON_NA = 8;
	}
	//End of initialization

	//The is the main branch predictor 
	branch_update *predict (branch_info & b) {
		bi = b;
		//Direction predictor
		if (b.br_flags & BR_CONDITIONAL) {
			u.cond_index = 
				  (history << (TABLE_BITS - HISTORY_LENGTH)) 
				^ (b.address & ((1<<TABLE_BITS)-1));
			u.direction_prediction (tab[u.cond_index] >> 1);
		//End of conditional branch predictor
		} else {
			u.direction_prediction (true);
		}
		//End of direction predictor
		
		//Indirect branch predictor
		if (b.br_flags & BR_INDIRECT) {
		u.found=0;
		u.index=0;
		int pred_counts =0;
			//Loop thorugh to see if any index matches
			for (int j=HISTORY_TABLES-1; j>=0 ; j--){				//Start looking through from the last table
					uint8_t temp_index;					
					temp_index = addr_fold(b.address);			//PC folded
					temp_index ^= CSR1[j];					//Xor PC folded with history folded to get index
					u.index = temp_index;
					if (valid[j][temp_index] == 1){				//Check if valid is 1
					uint8_t new_tag= ((b.address % (1<<8)) ^ CSR1[j] ^ (CSR2[j]<<1));//Compute tag
					if (tag[j][temp_index] == new_tag ){			//Check if tag matches
					if (pred_counts == 1){					//Find an altpred after pred is found
                                                        u.altpred_history_table = j;
                                                        u.altpred_index = temp_index;
                                                        pred_counts = 2;
							u.altpred_found = 1;
                                                        }

					if (pred_counts == 0){					//Find a pred
                                                        u.pred_history_table = j;
                                                        u.pred_index = temp_index;
                                                        u.found = 1;
                                                        pred_counts = 1;

                                                }
					if (pred_counts == 2) break;				//Break when both pred and altpred are found
				}								//End of tag match loop
				}								//End of valid match loop 
			}
			//End of looking through all tables

			if (u.found == 1){							//Atleast a pred is found
			if (USE_ALT_ON_NA > 7 and ctr[u.pred_history_table][u.pred_index] == 1 and useful[u.pred_history_table][u.pred_index] == 0 and pred_counts == 2 and ctr[u.altpred_history_table][u.altpred_index] > 0){		   						    //Use altpred when pred is a new value, altpred counter is more than 1 and USE_ALT_ON_NA > 7
				u.index = u.altpred_index;
				u.history_table = u.altpred_history_table;
			}
			else{
				u.index = u.pred_index;
				u.history_table = u.pred_history_table;
			}									//End of deciding between pred and altpred
		
				u.target_prediction (pred[u.history_table][u.index]);		//Make the prediciton
                                u.predicted_target = pred[u.history_table][u.index];
			}
			else{									//Predict from base table if pred is not found
			u.predicted_target = base_predictor[((b.address ^ previous_target) % (1<<TAG_BITS))];
		//	 u.predicted_target = base_predictor[b.address % (1<<TAG_BITS)];
			u.target_prediction (u.predicted_target);
			}
		
		}
		//End of indirect branch predictor
		return &u;
	}
	//End of predictor


	//Update the tables, tags, useful bits and counter when a feedback is received from the simulator. This is required to predict better in future	
	void update (branch_update *u, bool taken, unsigned int target) {
		
		//Conditional branch predictor update
		if (bi.br_flags & BR_CONDITIONAL) {
			unsigned char *c = &tab[((my_update*)u)->cond_index];
			if (taken) {
				if (*c < 3) (*c)++;
			} else {
				if (*c > 0) (*c)--;
			}
			history <<= 1;
			history |= taken;
			history &= (1<<HISTORY_LENGTH)-1;
		}
		//End of conditional branch predictor update

		//ITTAGE update
		if (bi.br_flags & BR_INDIRECT) {
		int allocate_values = 1;
		base_predictor[((bi.address ^ previous_target) % (1<<TAG_BITS))] = target;						//Always keep the base predictor upto date
		//base_predictor[bi.address % (1<<TAG_BITS)] = target;

		///If the predicition was from predicition tables and predictor target was correct, increment the counter	
		if (((my_update *)u)->predicted_target == target and ((my_update *)u)->found == 1){
			if (ctr[((my_update *)u)->history_table][int(((my_update *)u)->index)] <= 2) {
			ctr[((my_update *)u)->history_table][int(((my_update *)u)->index)]++;
			}
		} 	

		//If the predicition was from prediction tables, decrement if counter is not null, else replace the target value if null. Assign 3 new values
		//If prediction was from base table, assign 3 new values
		else{
			if (((my_update *)u)->found == 1){							//Prediciton from base table
				if ((pred[((my_update *)u)->pred_history_table][((my_update *)u)->pred_index] == target) and ( ((my_update *)u)->altpred_found == 1 and pred[((my_update *)u)->altpred_history_table][((my_update *)u)->altpred_index] != target)){					   //If pred was right and altpred was wrong, set the useful bit of pred
                                	useful[((my_update *)u)->pred_history_table][((my_update *)u)->pred_index] = 1;
					allocate_values = 0;
					if (USE_ALT_ON_NA > 0 ) USE_ALT_ON_NA--;				//Decrement USE_ALT_ON_NA so as to use pred components
				}
				if ((pred[((my_update *)u)->pred_history_table][((my_update *)u)->pred_index] != target) and ( ((my_update *)u)->altpred_found == 1 and pred[((my_update *)u)->altpred_history_table][((my_update *)u)->altpred_index] == target)){
				if (USE_ALT_ON_NA < 15) USE_ALT_ON_NA++;					//If pred was wrong and altpred was right, incremnet to use altpred
				}
				if (ctr[((my_update *)u)->history_table][int(((my_update *)u)->index)] >= 1) {
                        		ctr[((my_update *)u)->history_table][int(((my_update *)u)->index)]--;	//Decrement counter of wrong predictor
                        	}
                        	else {
                                	pred[((my_update *)u)->history_table][int(((my_update *)u)->index)] = target; //If counter is 0, relplace with new target, and update u, ctr and tag
					tag[((my_update *)u)->history_table][int(((my_update *)u)->index)] = ((bi.address % (1<<8)) ^ CSR1[((my_update *)u)->history_table] ^ (CSR2[((my_update *)u)->history_table]<<1));
					ctr[((my_update *)u)->history_table][int(((my_update *)u)->index)] = 1;
					useful[((my_update *)u)->history_table][int(((my_update *)u)->index)] = 0;
                        	}	
			}
			if(((my_update *)u)->found == 0 or allocate_values == 1){				//Allocate when base table predciton or when prediciton is wrong
				int alloted = 0;
                                int j;

				//Start allocating new entries
                                if (((my_update *)u)->found == 1){						//Start from table 1 more than the table that gave wrong prediction
					j=(((my_update *)u)->history_table+1);
                                }
                                else {										//For base table prediction
                                        j=0;
                                }
                                for (; j<HISTORY_TABLES; j++){
                                                uint8_t new_index = 0;
                                                new_index = addr_fold(bi.address);
						new_index ^= CSR1[j];
                                        if (useful[j][new_index] == 0){
						if (reset_counter<255) reset_counter++;
                                                pred[j][new_index] = target;
						tag[j][new_index] = ((bi.address % (1<<8)) ^ CSR1[j] ^ (CSR2[j]<<1));
                                                valid[j][new_index] = 1;
                                                ctr[j][new_index] = 1;
                                                alloted++;
						j++;								//Dont allot on consecutive tables
                                                if (alloted == 3) 						//Allocate 3 entries max
						break;
                                        }
		
					//Resetting the used bits - Keep the number of time a useful = 1 was hit and entry was not allowed to be allocated. Reset when the count is 255
					else{
					if (reset_counter > 0) reset_counter--;
					if (reset_counter == 0){
						for (int l=0; l<=(HISTORY_TABLES-1);l++){
                                                        for (int m=0; m<=((1<<8)-1);m++){
                                                                useful[l][m] = 0;
                                                        }
                                                }
                                        reset_counter = 128;		//Initialize the counter back on reset
					}
					}
					//End of reset policy
                                }
				//End of allocating new entries
				
			}
		}
		//end of if condition - if ((found and target does not match) or (base prediction))
	}
	//End of indirect branch predictor update policy	
	
		bool bit_update;
		bit_update = 0;
		//History bits update - For conditional branches, just push the taken/not-taken bit. For indirect branches, push the XOR of 10 bits address and 5 bits target
		if (bi.br_flags & BR_CONDITIONAL) {
		bit_update = taken;
		overflow_bit = indirect_history[(1<<(HISTORY_TABLES-1))-1] >> 7;
                for (int i=((1<<(HISTORY_TABLES-1))-1); i>0; i--){
                indirect_history[i] <<= 1;
                indirect_history[i] |= (indirect_history[i-1] >> 7);
                }
                indirect_history[0] <<= 1;
                indirect_history[0] |= bit_update;							//conditional branch - taken bit shifted into history bits

		//Compute CSR1 - Global history folded to 8 bits
                CSR1[0] = indirect_history[0];
		int shifted_bit = 0;
		for (int i=1; i<HISTORY_TABLES-1; i++){
			shifted_bit = CSR1[i]>>7;
			CSR1[i] <<= 1;
			CSR1[i] |= shifted_bit ^ (indirect_history[0] & 1) ^ (indirect_history[1<<i] & 1);
		}
		shifted_bit = CSR1[HISTORY_TABLES-1]>>7;
                CSR1[HISTORY_TABLES-1] <<= 1;
                CSR1[HISTORY_TABLES-1] |= shifted_bit ^ (indirect_history[0] & 1) ^ overflow_bit;
		//End of CSR1 computation

		//Compute CSR2 - Global history folded to 7 bits
		CSR2[0]= indirect_history[0];
		int pos0, pos1;
		for(int i=1; i<HISTORY_TABLES; i++){
		int value = 7*(1<<i);
                int table_no = value/8;
                int LSB_of_table = table_no*8;
		shifted_bit = (CSR2[i] >> 6) & 1;
		pos0 = shifted_bit ^ (indirect_history[0] & 1);
		pos1 = ((indirect_history[table_no] >> (value - LSB_of_table)) & 1);
		CSR2[i] <<=1;
		CSR2[i] |= pos0 ^ pos1;
		}
		//End of CSR2 computation
		}
		//End of history update for conditional branches

		//History update for indirect branches
		if (bi.br_flags & BR_INDIRECT) {
		previous_target = target;
                //int new_target = (bi.address%(1<<10)) ^ ((target%(1<<5))<<5);
                int new_target = (bi.address%(1<<10)) ^ ((target%(1<<5)));
                for (int i=((1<<(HISTORY_TABLES-1))+1); i>1; i--){
                indirect_history[i] = indirect_history[i-1];
		indirect_history[i] <<= 2;
		indirect_history[i] |= (indirect_history[i-2]>>6);
                }
		indirect_history[1] = indirect_history[0];
		indirect_history[1] <<= 2;
		indirect_history[1] |= new_target>>8;
		indirect_history[0] = new_target%(1<<8);
		
		//Compute CSR1 - History folded to 8 bits
		int pos1,shifted_bit;
                pos1=0;
                CSR1[0]=indirect_history[0];
                for (int i=1; i<HISTORY_TABLES; i++){
                        int ctr = 7;
			int ctr1 = 7;
                        int value = (1<<(i+3))+9;
                        int table_no = value/8;
                        int LSB_of_table = table_no*8;
                        for(int j=0; j<10; j++){
                                int new_bit = ((new_target >> (9-j))&1);
                                shifted_bit = ((CSR1[i] >> 7) & 1);
                                if ((value - LSB_of_table -j) >= 0){
                                        pos1 = ((indirect_history[table_no] >> (value - LSB_of_table - j)) & 1);
                                }
                                else{
					if (ctr < 0 ){
					pos1 = (indirect_history[table_no - 1] >> ctr1) & 1;
					ctr1--;
					}
                                        else{ 
					pos1 = (indirect_history[table_no - 1] >> ctr) & 1;
					ctr--;
					}
                                }
                                CSR1[i] <<= 1;
                                CSR1[i] |= (shifted_bit ^ new_bit ^ pos1);
                        }
                }
		//End of CSR1 computation
		
		
                //Compute CSR2 - History folded to 7 bits
                pos1=0;
                CSR2[0]=indirect_history[0];
                for (int i=1; i<HISTORY_TABLES; i++){
                        int ctr = 7;
                        int ctr1 = 7;
                        int value = (7* (1<<i)) + 9;
                        int table_no = value/8;
                        int LSB_of_table = table_no*8;
                        for(int j=0; j<10; j++){
                                int new_bit = ((new_target >> (9-j))&1);
                                shifted_bit = ((CSR2[i] >> 6) & 1);
                                if ((value - LSB_of_table -j) >= 0){
                                        pos1 = ((indirect_history[table_no] >> (value - LSB_of_table - j)) & 1);
                                }
                                else{
                                        if (ctr < 0 ){
                                        pos1 = (indirect_history[table_no - 1] >> ctr1) & 1;
                                        ctr1--;
                                        }
                                        else{
                                        pos1 = (indirect_history[table_no - 1] >> ctr) & 1;
                                        ctr--;
                                        }
                                }
                                CSR2[i] <<= 1;
                                CSR2[i] |= (shifted_bit ^ new_bit ^ pos1);
                        }
                }
		//End of CSR2 computation

		}
		//End of history update for indirect branches
	}
	//End of my update function
};
//End of my_predictor class
