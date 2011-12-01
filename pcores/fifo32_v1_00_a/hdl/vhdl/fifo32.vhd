library ieee;
use ieee.std_logic_1164.all;
use ieee.std_logic_arith.all;
use ieee.std_logic_unsigned.all;

entity fifo32 is
	generic (
		FIFO32_DEPTH : integer := 16
	);
	port (
		Rst : in std_logic;
		FIFO32_S_Clk : in std_logic;
		FIFO32_M_Clk : in std_logic;
		FIFO32_S_Data : out std_logic_vector(31 downto 0);
		FIFO32_M_Data : in std_logic_vector(31 downto 0);
		FIFO32_S_Fill : out std_logic_vector(15 downto 0);
		FIFO32_M_Rem : out std_logic_vector(15 downto 0);
		FIFO32_S_Rd : in std_logic;
		FIFO32_M_Wr : in std_logic
	);
end entity;

architecture implementation of fifo32 is
	type MEM_T is array (0 to FIFO32_DEPTH-1) of std_logic_vector(31 downto 0);
	signal mem : MEM_T;
	signal wrptr : std_logic_vector(15 downto 0);
	signal rdptr : std_logic_vector(15 downto 0);
	signal remainder : std_logic_vector(15 downto 0);
	signal fill      : std_logic_vector(15 downto 0);
begin
	FIFO32_S_Fill <= fill;
	FIFO32_M_Rem  <= remainder;
	FIFO32_S_Data <= mem(CONV_INTEGER(rdptr));
	
	fill <= wrptr - rdptr when wrptr >= rdptr else (FIFO32_DEPTH + wrptr) - rdptr;
	remainder <= (FIFO32_DEPTH - 1) - fill;
	
	process(FIFO32_M_Clk, Rst)
	begin
		if Rst = '1' then
			wrptr <= (others => '0');
		elsif rising_edge(FIFO32_M_Clk) then
			if FIFO32_M_Wr = '1' then
				mem(CONV_INTEGER(wrptr)) <= FIFO32_M_Data;
				if wrptr = FIFO32_DEPTH-1 then
					wrptr <= (others => '0');
				else
					wrptr <= wrptr + 1;
				end if;
			end if;
		end if;
	end process;

	process(FIFO32_S_Clk, Rst)
	begin
		if Rst = '1' then
			rdptr <= (others => '0');
		elsif rising_edge(FIFO32_S_Clk) then
			if FIFO32_S_Rd = '1' then			
				if rdptr = FIFO32_DEPTH-1 then
					rdptr <= (others => '0');
				else
					rdptr <= rdptr + 1;
				end if;
			end if;
		end if;
	end process;

end architecture;
