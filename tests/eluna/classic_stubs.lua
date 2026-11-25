-- E5: the systems Classic 1.60 lacks answer with the documented error instead of a nil method.
local ok_halaa, err_halaa = pcall(GetOwnerHalaa)
stub_error = ok_halaa and 'no error' or err_halaa
local ok_taxi, err_taxi = pcall(AddTaxiPath, { { 0, 1, 2, 3 } }, 1, 2)
taxi_error = ok_taxi and 'no error' or err_taxi
local ok_vehicle, err_vehicle = pcall(RegisterVehicleEvent, 1, function() end)
vehicle_error = ok_vehicle and 'no error' or err_vehicle

bg_registered = false
local cancel = RegisterBGEvent(3, function(event, bg, bgId, instanceId)
    bg_created = bgId
end)
bg_registered = type(cancel) == 'function'
