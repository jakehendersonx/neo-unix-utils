"use strict";

const cube = document.querySelector(".cube");
const toggle = document.querySelector("#toggle");
const reducedMotion = window.matchMedia("(prefers-reduced-motion: reduce)");
const animation = cube.animate([
	{ transform: "rotateX(-25deg) rotateY(35deg)" },
	{ transform: "rotateX(335deg) rotateY(395deg)" }
], { duration: 12000, iterations: Infinity });

function setPlaying(playing) {
	if (playing) animation.play();
	else animation.pause();
	toggle.textContent = playing ? "Pause rotation" : "Play rotation";
}

setPlaying(!reducedMotion.matches);
toggle.disabled = false;
toggle.addEventListener("click", () => setPlaying(animation.playState !== "running"));
reducedMotion.addEventListener("change", () => setPlaying(!reducedMotion.matches));
