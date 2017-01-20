function Tick( dt_param )
	dt = dt_param
	t = math.fmod( t + dt_param, 3.14159265359 * 2 )
	dofile( "src/core/main.lua" )
	PromoteKeys( )
end