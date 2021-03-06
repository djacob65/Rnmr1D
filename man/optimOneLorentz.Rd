% Generated by roxygen2: do not edit by hand
% Please edit documentation in R/deconvTools.R
\name{optimOneLorentz}
\alias{optimOneLorentz}
\title{optimOneLorentz}
\usage{
optimOneLorentz(X, Y, par)
}
\arguments{
\item{X}{a vector of ppm values}

\item{Y}{a vector of intensities}

\item{par}{a vector of the 3 lorentzian parameters namely: Amplitude, central ppm value, ppm width at mid-height}
}
\value{
a vector of the lorentzian parameters (same size as par)
}
\description{
\code{optimOneLorentz} belongs to the low-level functions group for deconvolution.
}
