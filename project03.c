#include <stdio.h>
#include <stdlib.h>
#include <string.h>

unsigned char memory_file[65536];

enum
{
    SET,
    ADD = 0x10,
    ADDI,
    SUB = 0x20,
    SUBI,
    MUL = 0x30,
    MULI,
    DIV = 0x40,
    DIVI,
    LDI = 0x50,
    LD,
    STI = 0x60,
    ST,
    BEZ = 0x70,
    BGEZ,
    BLEZ,
    BGTZ,
    BLTZ,
    RET = 0xFF
};

enum
{
    IF,
    ID,
    IA,
    EX1,
    BR,
    EX2,
    MEM1,
    MEM2,
    WB
};

char *inst_names[] = 
    {"set", "add", "sub", "mul", "div", "ld", "st", "bez", "bgez", "blez", "bgtz", "bltz", "ret"};

/* structure used to represent instructions */
typedef struct
{
    unsigned opcode;
    unsigned Rx, Ry, Rz;
}instruction;

typedef struct
{
    unsigned    PC;
    instruction instruction;
    int         ops[3];
    int         wb;
    int         valid;
}Pipeline;

void save_output()
{
    FILE *f;
    size_t nwrite;

    f = fopen("output_memory_map", "wb");

    memset(memory_file, 0, 999); /* clear code space */

    nwrite = fwrite(memory_file, 1, 65536, f); 
    fclose(f);
}

int load_word(unsigned address)
{
    int word;
    word = memory_file[address];
    word |= memory_file[address + 1] << 8;
    word |= memory_file[address + 2] << 16;
    word |= memory_file[address + 3] << 24;
    return word;
}

void save_word(unsigned address, int word)
{
    memory_file[address] = word;
    memory_file[address + 1] = word >> 8;
    memory_file[address + 2] = word >> 16;
    memory_file[address + 3] = word >> 24;
}

void print_inst_op(unsigned r, int sel, FILE *f)
{
    switch (sel)
    {
    case 0: break;
    case 1: fprintf(f, "R%d", r & 0xF); break;
    case 2: fprintf(f, "#%d", r); break;
    case 3: fprintf(f, "#%04d", r); break;
    }
}

void print_inst_ops(instruction instruction, int r1, int r2, int r3, FILE *f)
{
    if ((r2 == 2 && r3 == 2) || (r2 == 3 && r3 == 3))
    {
        print_inst_op(instruction.Rx, 1, f);
        fprintf(f, ", ");
        print_inst_op(instruction.Ry * 256 + instruction.Rz, r2, f);
    }
    else
    {
        print_inst_op(instruction.Rx, r1, f);
        if (r1 && (r2 || r3))
            fprintf(f, ", ");
        print_inst_op(instruction.Ry, r2, f);
        if (r2 && r3)
            fprintf(f, ", ");
        print_inst_op(instruction.Rz, r3, f);
    }
}

void print_instruction(instruction instruction, FILE *f)
{
    if (instruction.opcode >= SET && instruction.opcode <= DIVI)
    {
        fprintf(f, "%s ", inst_names[instruction.opcode >> 4]);
        if (instruction.opcode == SET)
	        print_inst_ops(instruction, 1, 2, 2, f);
        else if (instruction.opcode & 1)
            print_inst_ops(instruction, 1, 1, 2, f);
        else
            print_inst_ops(instruction, 1, 1, 1, f);
    }
    else if (instruction.opcode == LDI || instruction.opcode == LD || instruction.opcode == STI || instruction.opcode == ST)
    {
        fprintf(f, "%s ", inst_names[instruction.opcode >> 4]);
        if (instruction.opcode & 1)
            print_inst_ops(instruction, 1, 0, 1, f);
        else
            print_inst_ops(instruction, 1, 2, 2, f);
    }
    else if (instruction.opcode >= BEZ && instruction.opcode <= BLTZ)
    {
        fprintf(f, "%s ", inst_names[(instruction.opcode >> 4) + (instruction.opcode & 0xF) ]);
        print_inst_ops(instruction, 1, 3, 3, f);
    }
    else if (instruction.opcode == RET)
        fprintf(f, "%s", inst_names[12]);
    fprintf(f, "\n");
}

int main(int argc, char const *argv[]){
    FILE *f;
    size_t nread;
    unsigned size;

    int data_hazards = 0;
    int control_hazards = 0;
    int execution_cycles = 0;
    int completed_instructions = 0;

    FILE *log;
    unsigned PC = 0;
    instruction instruction;
    int regs[16];
    int valid[16];
    Pipeline Pipeline[9];
    int i;
    int stall;
    int branch_stall, newbranch_stall;
    int end;
    int reg;

    // READ FILE
    f = fopen(argv[1], "rb");
    fseek(f, 0, 2);
    size = ftell(f);
    fseek(f, 0, 0);

    nread = fread(memory_file, 1, size, f);  
    fclose(f);

    log = fopen("logfile.txt", "wt");
    if (log == NULL)
    {
        printf("Unable to create log file\n");
        exit(EXIT_FAILURE);
    }

    for(i = 0; i < 16; i++){
        regs[i] = 0;
        valid[i] = 1;
    }

    for (i = 0; i < 9; i++)
    {
        Pipeline[i].valid = 0;
        Pipeline[i].wb = 0;
    }
    Pipeline[IF].valid = 1;

    execution_cycles = 0;

    stall = 0;
    newbranch_stall = 0;
    branch_stall = 0;
    end = 0;

    while (!end)
    {

        fprintf(log, "================================\n");
        fprintf(log, "Clock Cycle #: %d\n", execution_cycles + 1);
        fprintf(log, "--------------------------------\n");
        if (Pipeline[WB].valid)
        {
            if (Pipeline[WB].wb)
            {
                regs[Pipeline[WB].ops[0]] = Pipeline[WB].ops[1];
                valid[Pipeline[WB].ops[0]] = 1;
            }
            if (Pipeline[WB].instruction.opcode == RET)
            {
                end = 1;
            }
            completed_instructions++;
            fprintf(log, "WB             : %04d ", Pipeline[WB].PC);
            print_instruction(Pipeline[WB].instruction, log);
        }
        if (Pipeline[MEM2].valid)
        {
            switch (Pipeline[MEM2].instruction.opcode)
            {
            case LD: case LDI:
                Pipeline[MEM2].ops[1] = load_word(Pipeline[MEM2].ops[1]);
                break;
            case ST: case STI:
                save_word(Pipeline[MEM2].ops[1], Pipeline[MEM2].ops[0]);
                break;
            }
            fprintf(log, "Mem2           : %04d ", Pipeline[MEM2].PC);
            print_instruction(Pipeline[MEM2].instruction, log);
        }
        if (Pipeline[MEM1].valid)
        {
            fprintf(log, "Mem1           : %04d ", Pipeline[MEM1].PC);
            print_instruction(Pipeline[MEM1].instruction, log);
        }
        if (Pipeline[EX2].valid)
        {
            switch (Pipeline[EX2].instruction.opcode)
            {
            case MUL: case MULI:
                Pipeline[EX2].ops[1] = Pipeline[EX2].ops[1] * Pipeline[EX2].ops[2];
                break;
            case DIV: case DIVI:
                Pipeline[EX2].ops[1] = Pipeline[EX2].ops[1] / Pipeline[EX2].ops[2];
                break;
            }
            fprintf(log, "Ex2            : %04d ", Pipeline[EX2].PC);
            print_instruction(Pipeline[EX2].instruction, log);
        }
        if (Pipeline[BR].valid)
        {
            switch (Pipeline[BR].instruction.opcode)
            {
            case BEZ: 
                PC = PC + 4;
                if (Pipeline[EX2].ops[0] == 0)
                    PC = Pipeline[EX2].ops[1];
                newbranch_stall = 0;
                break;
            case BGEZ: 
                PC = PC + 4;
                if (Pipeline[EX2].ops[0] >= 0)
                    PC = Pipeline[EX2].ops[1];
                newbranch_stall = 0;
                break;
            case BLEZ: 
                PC = PC + 4;
                if (Pipeline[EX2].ops[0] <= 0)
                    PC = Pipeline[EX2].ops[1];
                newbranch_stall = 0;
                break;
            case BGTZ: 
                PC = PC + 4;
                if (Pipeline[EX2].ops[0] > 0)
                    PC = Pipeline[EX2].ops[1];
                newbranch_stall = 0;
                break;
            case BLTZ: 
                PC = PC + 4;
                if (Pipeline[EX2].ops[0] < 0)
                    PC = Pipeline[EX2].ops[1];
                newbranch_stall = 0;
                break;
            }
            fprintf(log, "Br             : %04d ", Pipeline[BR].PC);
            print_instruction(Pipeline[BR].instruction, log);
        }
        if (Pipeline[EX1].valid)
        {
            switch (Pipeline[EX1].instruction.opcode)
            {
            case SET:
                break;
            case ADD: case ADDI:
                Pipeline[EX1].ops[1] = Pipeline[EX1].ops[1] + Pipeline[EX1].ops[2];
                break;
            case SUB: case SUBI:
                Pipeline[EX1].ops[1] = Pipeline[EX1].ops[1] - Pipeline[EX1].ops[2];
                break;
            }
            fprintf(log, "Ex1            : %04d ", Pipeline[EX1].PC);
            print_instruction(Pipeline[EX1].instruction, log);
        }
        if (Pipeline[IA].valid)
        {
            Pipeline[IA].wb = 1;

            instruction = Pipeline[IA].instruction;
            if (instruction.opcode == SET)
            {
                Pipeline[IA].ops[0] = instruction.Rx & 0xF;
                Pipeline[IA].ops[1] = instruction.Ry * 256 + instruction.Rz;
                
            }
            else if (instruction.opcode >= ADD && instruction.opcode <= DIVI)
            {
                instruction.Rx &= 0xF;
                instruction.Ry &= 0xF;
                Pipeline[IA].ops[0] = instruction.Rx;

                if (instruction.opcode & 1)
                {
                    if (valid[instruction.Ry] && (!Pipeline[WB].valid || !Pipeline[WB].wb || Pipeline[WB].ops[0] != instruction.Ry))
                        stall = 0;
                    else
                        stall = 1;

                    Pipeline[IA].ops[1] = regs[instruction.Ry];
                    Pipeline[IA].ops[2] = instruction.Rz;
                }
                else
                {
                    instruction.Rz &= 0xF;

                    if (valid[instruction.Ry] && valid[instruction.Rz] && (!Pipeline[WB].valid || !Pipeline[WB].wb || (Pipeline[WB].ops[0] != instruction.Ry
                            && Pipeline[WB].ops[0] != instruction.Rz)))
                        stall = 0;
                    else
                        stall = 1;

                    Pipeline[IA].ops[1] = regs[instruction.Ry];
                    Pipeline[IA].ops[2] = regs[instruction.Rz];
                }
            }
            else if (instruction.opcode == LDI || instruction.opcode == LD)
            {
                instruction.Rx &= 0xF;
                Pipeline[IA].ops[0] = instruction.Rx;

                if (instruction.opcode == LDI)
                    Pipeline[IA].ops[1] = instruction.Ry * 256 + instruction.Rz;
                else
                {
                    instruction.Rz &= 0xF;
                    if (valid[instruction.Rz] && (!Pipeline[WB].valid || !Pipeline[WB].wb || Pipeline[WB].ops[0] != instruction.Rz))
                        stall = 0;
                    else
                        stall = 1;
                    Pipeline[IA].ops[1] = regs[instruction.Rz];
                }
            }
            else if (instruction.opcode == STI || instruction.opcode == ST)
            {
                instruction.Rx &= 0xF;

                Pipeline[IA].wb = 0;
                Pipeline[IA].ops[0] = regs[instruction.Rx];
                if (instruction.opcode == STI)
                {
                    if (valid[instruction.Rx] && (!Pipeline[WB].valid || !Pipeline[WB].wb || Pipeline[WB].ops[0] != instruction.Rx))
                        stall = 0;
                    else
                        stall = 1;
                    Pipeline[IA].ops[1] = instruction.Ry * 256 + instruction.Rz;
                }
                else
                {
                    instruction.Rz &= 0xF;

                    if (valid[instruction.Rx] && valid[instruction.Rz] && (!Pipeline[WB].valid || !Pipeline[WB].wb || (Pipeline[WB].ops[0] != instruction.Rx && Pipeline[WB].ops[0] != instruction.Rz)))
                        stall = 0;
                    else
                        stall = 1;
                    Pipeline[IA].ops[1] = regs[instruction.Rz];
                }
            }
            else if (instruction.opcode >= BEZ && instruction.opcode <= BLTZ)
            {
                instruction.Rx &= 0xF;

                if (valid[instruction.Rx] && (!Pipeline[WB].valid || !Pipeline[WB].wb || Pipeline[WB].ops[0] != instruction.Rx))
                    stall = 0;
                else
                    stall = 1;

                Pipeline[IA].wb = 0;
                Pipeline[IA].ops[0] = regs[instruction.Rx];
                Pipeline[IA].ops[1] = instruction.Ry * 256 + instruction.Rz;
            }
            else if (instruction.opcode == RET)
                Pipeline[IA].wb = 0;
            if (!stall)
            {
                if (Pipeline[IA].wb)
                    valid[Pipeline[IA].ops[0]] = 0;
            }
            else
                Pipeline[IA].valid = 0;

            fprintf(log, "IA             : %04d ", Pipeline[IA].PC);
            print_instruction(Pipeline[IA].instruction, log);
        }
        if (Pipeline[ID].valid)
        {
            fprintf(log, "ID             : %04d ", Pipeline[ID].PC);
            print_instruction(Pipeline[ID].instruction, log);
        }

        if (PC < size && !branch_stall)
        {
            Pipeline[IF].PC = PC;
            Pipeline[IF].instruction.opcode = memory_file[PC + 3];
            Pipeline[IF].instruction.Rx = memory_file[PC + 2];
            Pipeline[IF].instruction.Ry = memory_file[PC + 1];
            Pipeline[IF].instruction.Rz = memory_file[PC];
            Pipeline[IF].valid = 1;
            fprintf(log, "IF             : %04d ", Pipeline[IF].PC);
            print_instruction(Pipeline[IF].instruction, log);
            if (Pipeline[IF].instruction.opcode >= BEZ && Pipeline[IF].instruction.opcode <= BLTZ)
                newbranch_stall = 1;
            else if (!stall)
                PC += 4;
        }
        else
            Pipeline[IF].valid = 0;
        for (i = 9 - 1; i > ((stall)? IA : 0); i--)
            Pipeline[i] = Pipeline[i - 1];
        if (stall)
            Pipeline[IA].valid = 1;

        branch_stall = newbranch_stall;

        execution_cycles++;
        if (stall)
            data_hazards++;
        else if (branch_stall)
            control_hazards++;


        fprintf(log, "=============== STATE OF ARCHITECTURAL REGISTER FILE ==========\n\n");
        for (reg=0; reg<16; reg++) {
            fprintf(log, "|   REG[%2d]   |   Value=%d   |   Status=%s   |\n",reg, regs[reg], (valid[reg])? "valid" : "writing");
        }
    }
    fprintf(log, "================================\n");
    fprintf(log, "\n");
    fprintf(log, "================================\n");
    fprintf(log, "--------------------------------\n");
    for (reg=0; reg<16; reg++) {
        fprintf(log, "REG[%2d]   |   Value=%d  \n",reg, regs[reg]);
        fprintf(log, "--------------------------------\n");
    }
    fprintf(log, "================================\n\n");

    fprintf(log, "Stalled cycles due to data hazard: %d \n", data_hazards);
    fprintf(log, "Stalled cycles due to control hazard: %d \n", control_hazards);
    fprintf(log, "\n");
    fprintf(log, "Total stalls: %d \n", data_hazards+control_hazards);
    fprintf(log, "Total execution cycles: %d\n", execution_cycles);
    fprintf(log, "Total instruction simulated: %d\n", completed_instructions);
    fprintf(log, "IPC: %f\n", ((double)completed_instructions/execution_cycles));

    fclose(log);

    save_output();

    printf("================================\n");
    printf("--------------------------------\n");
    for (reg=0; reg<16; reg++) {
        printf("REG[%2d]   |   Value=%d  \n",reg, regs[reg]);
        printf("--------------------------------\n");
    }
    printf("================================\n\n");

    printf("Stalled cycles due to data hazard: %d \n", data_hazards);
    printf("Stalled cycles due to control hazard: %d \n", control_hazards);
    printf("\n");
    printf("Total stalls: %d \n", data_hazards+control_hazards);
    printf("Total execution cycles: %d\n", execution_cycles);
    printf("Total instruction simulated: %d\n", completed_instructions);
    printf("IPC: %f", ((double)completed_instructions/execution_cycles));
    fflush(stdout);

    return 0;
}
