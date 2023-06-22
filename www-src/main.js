
function addMessage(m)
{
	console.log(m);
	$("#console").append(m+"</br>");
}

function sendCommand(c)
{
	console.log(c);
	$.ajax({
		url:"post",
		type:"POST",
		data:{cmd: c},
		dataType:"html",
		success:function(data) {
			addMessage('Cmd: ' + data);
		}
	});
}

function getCnt()
{
	$.ajax({
		url:"cnt",
		type:"GET",
		dataType: "json",
		success:function(data) {
			console.log(data);
			const cnt = data.cnt;
			if (cnt == 0) {
				$('#but_cut').html('TNIJ');
				$('#but_cut').prop('disabled', false);
			} else {
				$('#but_cut').html(`${cnt}`);
				$('#but_cut').prop('disabled', true);
			}
		}
	});
}


function startEvents() 
{
	var es = new EventSource('/events');
	es.onopen = function(e) {
		addMessage("Events Opened");
	};
	es.onerror = function(e) {
		if (e.target.readyState != EventSource.OPEN) {
			addMessage("Events Closed");
		}
	};
	es.onmessage = function(e) {
		addMessage("Event: " + e.data);
	};
	es.addEventListener('cmd', function(e) {
		addMessage("Event[cmd]: " + e.data);
		if (e.data == "OK") {
			//sendCommand();
		}
	}, false);
}

$(document).ready(function()
{
	const speed = 16; /* 60 mm/s */
	startEvents();
	$("#but_left").click(function(){ sendCommand("MR,1000,-1");});
	$("#but_right").click(function(){ sendCommand("MR,1000,1");});
	$("#but_left2").click(function(){ sendCommand("MR,2000,-2");});
	$("#but_right2").click(function(){ sendCommand("MR,2000,2");});
	$("#but_left5").click(function(){ sendCommand("GTR,500,-1600");});
	$("#but_right5").click(function(){ sendCommand("GTR,500,1600");});
	$("#but_stop").click(function(){ sendCommand("STP");});
	$("#but_home").click(function(){ sendCommand("TC");});
	$("#but_scut").click(function(){ sendCommand("CT"); });
	$("#but_ref").click(function(){ 
		const cnt = 1;
		const len_mm = 100;
		const microsteps = 100 * len_mm;
		const duration = len_mm * speed;
		sendCommand(`CUT,${cnt},${microsteps},${duration}`);
	});	
	$("#but_cut").click(function() { 
		const len_mm = Math.round( $("#wire_mm").val() );
		const cnt = Math.round($("#wire_cnt").val());
		const ref = $("#wire_ref").val() * 1.0;
		const duration = Math.round(len_mm * speed);
		const microsteps = Math.round( (100.0 * len_mm * 100.0) / ref)  ;
		sendCommand(`CUT,${cnt},${microsteps},${duration}`);
	});
	$("#but_send").click(function(){ sendCommand($("#cmd").val());});
	setInterval(getCnt, 1000);
});