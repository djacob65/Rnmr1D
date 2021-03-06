% Generated by roxygen2: do not edit by hand
% Please edit documentation in R/ggplotTools.R
\name{ggplotLoadings}
\alias{ggplotLoadings}
\title{plotLoadings}
\usage{
ggplotLoadings(
  data,
  pc1 = 1,
  pc2 = 2,
  EV = NULL,
  associations = NULL,
  main = "Loadings",
  onlylabels = FALSE,
  highlabels = FALSE,
  gcontour = "ellipse"
)
}
\arguments{
\item{data}{the matrix of variable loadings coming from a multivariable analysis, typically a Principal Component Analysis (PCA)}

\item{pc1}{the fist component of the matrix of variable loadings to be plotted.}

\item{pc2}{the second component of the matrix of variable loadings to be plotted.}

\item{EV}{Eigenvalues vector}

\item{associations}{the associations matrix that gives for each cluster (column 2) the corresponding buckets (column 1). See \code{getClusters}}

\item{main}{Change the default plot title on the rigth corner}

\item{onlylabels}{if TRUE, put only the association names without drawing the cluster contours. Implies that association matrix is provided.}

\item{highlabels}{if TRUE, put the the association names in blue, and others in grey. Implies that association matrix is provided and fONLYLABELS equal to TRUE.}

\item{gcontour}{type of contour; possible values are : 'ellipse', 'polygon', 'ellipse2', 'none'}
}
\description{
Plots the two components defined by pc1, pc2 of the matrix of variable loadings coming from a 
multivariable analysis, typically a Principal Component Analysis (PCA).
It can also plot the ellipses corresponding to each cluster defined by the associations matrix 
if not null. (in fact it is the main interest of this function).
}
